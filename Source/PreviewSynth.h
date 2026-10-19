#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>

/** A small built-in sound so you can hear clips without routing MIDI anywhere.
    Melodic notes get a soft electric-piano-ish tone; drum mode gets a tiny drum kit
    (kick / snare / clap / hats / cymbals / toms laid out like General MIDI). */
class PreviewSound : public juce::SynthesiserSound
{
public:
    bool appliesToNote (int) override    { return true; }
    bool appliesToChannel (int) override { return true; }
};

class PreviewVoice : public juce::SynthesiserVoice
{
public:
    explicit PreviewVoice (const std::atomic<bool>& drumModeFlag) : drumMode (drumModeFlag) {}

    bool canPlaySound (juce::SynthesiserSound* s) override { return dynamic_cast<PreviewSound*> (s) != nullptr; }

    void startNote (int midiNote, float velocity, juce::SynthesiserSound*, int) override
    {
        note = midiNote;
        level = 0.18f * velocity;
        drum = drumMode.load();
        phase = phase2 = 0.0;
        time = 0.0;
        hp = prevNoise = 0.0f;
        freq = juce::MidiMessage::getMidiNoteInHertz (midiNote);

        env.setSampleRate (getSampleRate());
        env.setParameters ({ 0.004f, 0.6f, 0.45f, 0.25f });
        env.noteOn();
    }

    void stopNote (float, bool allowTailOff) override
    {
        if (drum && allowTailOff)
            return; // drum hits are one-shots and finish on their own

        if (allowTailOff)
        {
            env.noteOff();
        }
        else
        {
            env.reset();
            clearCurrentNote();
        }
    }

    void pitchWheelMoved (int) override {}
    void controllerMoved (int, int) override {}

    void renderNextBlock (juce::AudioBuffer<float>& out, int start, int num) override
    {
        if (getCurrentlyPlayingNote() < 0)
            return;

        const double sr = getSampleRate();
        const double dt = 1.0 / sr;

        for (int i = 0; i < num; ++i)
        {
            float v;
            bool finished = false;

            if (drum)
            {
                v = drumSample (sr) * level * 1.6f;
                if (time > 1.6) finished = true;
            }
            else
            {
                const float e = env.getNextSample();
                const double bright = std::exp (-time * 3.0);
                v = (float) (std::sin (phase)
                             + (0.35 + 0.3 * bright) * std::sin (2.0 * phase)
                             + 0.15 * bright * std::sin (3.0 * phase)
                             + 0.08 * std::sin (phase2)) * e * level;
                phase  += juce::MathConstants<double>::twoPi * freq * dt;
                phase2 += juce::MathConstants<double>::twoPi * freq * 1.003 * dt;
                if (! env.isActive()) finished = true;
            }

            time += dt;
            for (int ch = 0; ch < out.getNumChannels(); ++ch)
                out.addSample (ch, start + i, v);

            if (finished)
            {
                clearCurrentNote();
                break;
            }
        }
    }

private:
    float noise() { return random.nextFloat() * 2.0f - 1.0f; }

    float highNoise()
    {
        const float n = noise();
        hp = 0.6f * (hp + n - prevNoise);
        prevNoise = n;
        return hp;
    }

    float drumSample (double sr)
    {
        const double t = time;
        auto tone = [&] (double f)
        {
            phase += juce::MathConstants<double>::twoPi * f / sr;
            return (float) std::sin (phase);
        };

        if (note == 35 || note == 36)                                  // kick
            return tone (48.0 + 120.0 * std::exp (-t * 32.0)) * (float) std::exp (-t * 7.0);

        if (note == 38 || note == 40)                                  // snare
            return (0.45f * tone (185.0) * (float) std::exp (-t * 22.0))
                 + (0.65f * noise() * (float) std::exp (-t * 16.0));

        if (note == 37 || note == 39)                                  // rim / clap
            return noise() * (float) (std::exp (-t * 30.0) + 0.4 * std::exp (-std::abs (t - 0.012) * 300.0));

        if (note == 42 || note == 44)                                  // closed / pedal hat
            return 0.7f * highNoise() * (float) std::exp (-t * 45.0);

        if (note == 46)                                                // open hat
            return 0.6f * highNoise() * (float) std::exp (-t * 7.0);

        if (note >= 49 && note <= 59)                                  // cymbals
            return 0.45f * highNoise() * (float) std::exp (-t * 3.0);

        if (note >= 41 && note <= 50)                                  // toms
            return tone ((70.0 + (note - 41) * 18.0) * (1.0 + 0.5 * std::exp (-t * 20.0))) * (float) std::exp (-t * 9.0);

        // Anything else: a short pitched blip so nothing is silent.
        return tone (freq) * (float) std::exp (-t * 12.0);
    }

    const std::atomic<bool>& drumMode;
    juce::ADSR env;
    juce::Random random;
    int note = 60;
    bool drum = false;
    float level = 0.0f, hp = 0.0f, prevNoise = 0.0f;
    double phase = 0.0, phase2 = 0.0, time = 0.0, freq = 440.0;
};
