// Headless checks for the MIDI engine: file round trip, generation, drum mode and
// audio-to-MIDI transcription on synthesized audio. Build with -DMIMIC_BUILD_TESTS=ON.

#include "../Source/Engine/Phrase.h"
#include "../Source/Engine/MidiIO.h"
#include "../Source/Engine/Generator.h"
#include "../Source/Engine/AudioToMidi.h"

#include <iostream>
#include <set>

using namespace mm;

static int failures = 0;

static void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  [ok]   " : "  [FAIL] ") << what << std::endl;
    if (! ok) ++failures;
}

static Phrase makeMelodicClip()
{
    // 4 bars in A minor: Am - F - C - G pads plus a 16th-ish melody.
    Phrase p;
    p.bpm = 140;
    const int chords[4][3] = { { 57, 60, 64 }, { 53, 57, 60 }, { 48, 52, 55 }, { 55, 59, 62 } };
    const int melody[16]   = { 76, 74, 72, 74, 72, 69, 72, 76, 79, 76, 74, 72, 74, 71, 67, 71 };

    for (int bar = 0; bar < 4; ++bar)
    {
        for (int k = 0; k < 3; ++k)
            p.notes.push_back ({ chords[bar][k], bar * 4.0, 3.5, 0.7f, 1 });

        for (int i = 0; i < 4; ++i)
        {
            const double beat = bar * 4.0 + (i < 2 ? i * 0.75 : 1.5 + (i - 2) * 1.0);
            p.notes.push_back ({ melody[bar * 4 + i], beat, 0.5, 0.9f, 1 });
        }
    }
    p.sort();
    p.computeLengthFromNotes();
    p.name = "test-melodic";
    return p;
}

static Phrase makeDrumClip()
{
    Phrase p;
    p.bpm = 140;
    for (int bar = 0; bar < 2; ++bar)
    {
        const double o = bar * 4.0;
        for (int st : { 0, 10 }) p.notes.push_back ({ 36, o + st * 0.25, 0.125, 1.0f, 10 });
        if (bar == 1) p.notes.push_back ({ 36, o + 13 * 0.25, 0.125, 0.8f, 10 });
        for (int st : { 4, 12 }) p.notes.push_back ({ 38, o + st * 0.25, 0.125, 0.95f, 10 });
        for (int st = 0; st < 16; st += 2) p.notes.push_back ({ 42, o + st * 0.25, 0.125, st % 4 == 0 ? 0.8f : 0.55f, 10 });
    }
    p.sort();
    p.computeLengthFromNotes();
    p.isDrums = MidiIO::looksLikeDrums (p);
    return p;
}

static juce::AudioBuffer<float> renderTones (const std::vector<Note>& notes, double bpm, double sr, double seconds)
{
    juce::AudioBuffer<float> buf (1, (int) (sr * seconds));
    buf.clear();
    auto* d = buf.getWritePointer (0);
    for (auto& n : notes)
    {
        const double f = 440.0 * std::pow (2.0, (n.pitch - 69) / 12.0);
        const int s0 = (int) (n.start * 60.0 / bpm * sr);
        const int len = (int) (n.length * 60.0 / bpm * sr);
        for (int i = 0; i < len && s0 + i < buf.getNumSamples(); ++i)
        {
            const double t = i / sr;
            const double env = std::min (1.0, t / 0.005) * std::exp (-t * 2.0) * (i > len - 200 ? (len - i) / 200.0 : 1.0);
            double v = 0;
            for (int h = 1; h <= 6; ++h) v += std::sin (juce::MathConstants<double>::twoPi * f * h * t) / h;
            d[s0 + i] += (float) (0.2 * env * v * n.velocity);
        }
    }
    return buf;
}

static juce::AudioBuffer<float> renderDrums (const Phrase& p, double sr, double seconds)
{
    juce::AudioBuffer<float> buf (1, (int) (sr * seconds));
    buf.clear();
    auto* d = buf.getWritePointer (0);
    juce::Random r (5);
    juce::IIRFilter hatHp1, hatHp2, snareBp;
    hatHp1.setCoefficients (juce::IIRCoefficients::makeHighPass (sr, 7000.0));
    hatHp2.setCoefficients (juce::IIRCoefficients::makeHighPass (sr, 7000.0));
    snareBp.setCoefficients (juce::IIRCoefficients::makeBandPass (sr, 1800.0, 0.7));
    for (auto& n : p.notes)
    {
        const int s0 = (int) (n.start * 60.0 / p.bpm * sr);
        const int len = (int) (0.25 * sr);
        double phase = 0;
        for (int i = 0; i < len && s0 + i < buf.getNumSamples(); ++i)
        {
            const double t = i / sr;
            float v = 0;
            if (n.pitch == 36)
            {
                const double f = 50.0 + 110.0 * std::exp (-t * 30.0);
                phase += juce::MathConstants<double>::twoPi * f / sr;
                v = (float) (std::sin (phase) * std::exp (-t * 9.0));
            }
            else if (n.pitch == 38)
            {
                phase += juce::MathConstants<double>::twoPi * 200.0 / sr;
                const float noise = snareBp.processSingleSampleRaw (r.nextFloat() * 2 - 1);
                v = (float) ((0.5 * std::sin (phase) + 1.2 * noise) * std::exp (-t * 18.0));
            }
            else
            {
                const float noise = hatHp2.processSingleSampleRaw (hatHp1.processSingleSampleRaw (r.nextFloat() * 2 - 1));
                v = (float) (0.8 * noise * std::exp (-t * 60.0));
            }
            d[s0 + i] += v * n.velocity * 0.5f;
        }
    }
    return buf;
}

int main()
{
    auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("mimic-tests");
    tmp.createDirectory();

    std::cout << "MIDI file round trip" << std::endl;
    auto src = makeMelodicClip();
    {
        juce::String err;
        auto f = tmp.getChildFile ("source.mid");
        check (MidiIO::save (src, f, err), "save .mid " + err);
        Phrase back;
        check (MidiIO::load (f, back, err), "load .mid " + err);
        check (back.notes.size() == src.notes.size(), "note count survives (" + juce::String ((int) back.notes.size()) + ")");
        check (std::abs (back.bpm - 140.0) < 0.01, "tempo survives");
        check (std::abs (back.lengthBeats - 16.0) < 1e-6, "length is 4 bars");
        bool same = back.notes.size() == src.notes.size();
        for (size_t i = 0; same && i < src.notes.size(); ++i)
            same = back.notes[i].pitch == src.notes[i].pitch && std::abs (back.notes[i].start - src.notes[i].start) < 1e-3;
        check (same, "pitches and timing survive");
        Phrase mem;
        check (MidiIO::fromMemory (MidiIO::toMemory (src), mem) && mem.notes.size() == src.notes.size(), "memory round trip (plugin state)");
    }

    std::cout << "Melodic generation" << std::endl;
    {
        StyleModel model;
        model.analyse (src, StyleMode::Auto);
        check (model.isReady() && ! model.isDrums(), "analysed as melodic");
        check (model.getKey().name() == "A minor" || model.getKey().name() == "C major", "key detected: " + model.getKey().name());

        GenParams gp;
        gp.seed = 42;
        auto a = model.generate (gp);
        auto b = model.generate (gp);
        check (! a.empty(), "generates notes (" + juce::String ((int) a.notes.size()) + ")");
        check (a.notes.size() == b.notes.size(), "same seed = same result");
        check (std::abs (a.lengthBeats - 16.0) < 1e-6, "default length matches source");

        auto scale = model.getKey().scale();
        bool inKey = true;
        for (auto& n : a.notes) inKey &= scale[(size_t) (n.pitch % 12)];
        check (inKey, "lock-to-key keeps every note in key");

        double hi = 0, lo = 0;
        for (juce::uint32 seed = 1; seed <= 20; ++seed)
        {
            gp.seed = seed;
            gp.similarity = 0.95f; hi += similarityScore (src, model.generate (gp));
            gp.similarity = 0.15f; lo += similarityScore (src, model.generate (gp));
        }
        hi /= 20; lo /= 20;
        check (hi > lo + 0.1, "similarity knob works (avg match " + juce::String (hi, 2) + " vs " + juce::String (lo, 2) + ")");

        gp.similarity = 0.6f; gp.bars = 8; gp.seed = 7;
        auto longer = model.generate (gp);
        check (std::abs (longer.lengthBeats - 32.0) < 1e-6, "8-bar output");
        bool inRange = true;
        for (auto& n : longer.notes) inRange &= (n.start >= 0 && n.start + n.length <= 32.0 + 1e-6);
        check (inRange, "all notes inside the clip");

        gp.bars = 0; gp.transpose = 3;
        auto up = model.generate (gp);
        check (! up.empty(), "transposed output");

        juce::String err;
        check (MidiIO::save (longer, tmp.getChildFile ("generated.mid"), err), "export generated .mid");
    }

    std::cout << "Drum generation" << std::endl;
    {
        auto drums = makeDrumClip();
        check (drums.isDrums, "channel-10 clip detected as drums");
        StyleModel model;
        model.analyse (drums, StyleMode::Auto);
        check (model.isDrums(), "analysed as drums");

        GenParams gp;
        gp.similarity = 0.6f; gp.creativity = 0.7f; gp.bars = 8; gp.seed = 3;
        auto out = model.generate (gp);
        std::set<int> kit;
        for (auto& n : out.notes) kit.insert (n.pitch);
        check (! out.empty() && kit.size() <= 3, "uses only the source kit (" + juce::String ((int) kit.size()) + " voices, " + juce::String ((int) out.notes.size()) + " hits)");

        gp.similarity = 1.0f; gp.creativity = 0.0f; gp.bars = 0; gp.humanize = 0;
        auto same = model.generate (gp);
        check (same.notes.size() == drums.notes.size(), "similarity 100% reproduces the pattern");
    }

    std::cout << "Audio to MIDI (pitched)" << std::endl;
    {
        Phrase line;
        line.bpm = 120;
        const int pitches[8] = { 57, 60, 64, 67, 65, 64, 62, 60 };
        for (int i = 0; i < 8; ++i) line.notes.push_back ({ pitches[i], 1.0 + i * 1.0, 0.9, 0.9f, 1 });
        auto audio = renderTones (line.notes, 120, 44100, 6.0);

        TranscribeOptions opt;
        opt.maxPolyphony = 1;
        opt.bpm = 120;
        Phrase out; juce::String err;
        const bool ok = AudioToMidi::transcribeBuffer (audio, 44100, opt, out, err);
        check (ok, "transcribed " + err);

        int matched = 0;
        for (int i = 0; i < 8; ++i)
            for (auto& n : out.notes)
                if (n.pitch == pitches[i] && std::abs (n.start - i * 1.0) < 0.3) { ++matched; break; }
        check (matched >= 7, "melody recovered: " + juce::String (matched) + "/8 notes right (" + juce::String ((int) out.notes.size()) + " found)");
    }

    std::cout << "Audio to MIDI (chords)" << std::endl;
    {
        std::vector<Note> chords;
        const int prog[4][3] = { { 57, 60, 64 }, { 53, 57, 60 }, { 48, 52, 55 }, { 55, 59, 62 } };
        for (int c = 0; c < 4; ++c)
            for (int k = 0; k < 3; ++k) chords.push_back ({ prog[c][k], c * 2.0, 1.9, 0.8f, 1 });
        auto audio = renderTones (chords, 120, 44100, 4.5);

        TranscribeOptions opt;
        opt.maxPolyphony = 4;
        opt.bpm = 120;
        Phrase out; juce::String err;
        check (AudioToMidi::transcribeBuffer (audio, 44100, opt, out, err), "transcribed " + err);

        int matched = 0;
        for (auto& n : chords)
            for (auto& o : out.notes)
                if (o.pitch == n.pitch && std::abs (o.start - n.start) < 0.3) { ++matched; break; }
        check (matched >= 9, "chord notes recovered: " + juce::String (matched) + "/12 (" + juce::String ((int) out.notes.size()) + " found)");
    }

    std::cout << "Audio to MIDI (drums + tempo)" << std::endl;
    {
        auto drums = makeDrumClip();
        drums.bpm = 140;
        Phrase looped = drums;
        for (auto n : drums.notes) { n.start += 8.0; looped.notes.push_back (n); }   // 4 bars
        auto audio = renderDrums (looped, 44100, 16 * 60.0 / 140 + 0.5);

        TranscribeOptions opt;
        opt.drums = true;
        Phrase out; juce::String err;
        check (AudioToMidi::transcribeBuffer (audio, 44100, opt, out, err), "transcribed " + err);
        check (std::abs (out.bpm - 140) < 2 || std::abs (out.bpm - 70) < 1, "tempo detected: " + juce::String (out.bpm, 1) + " BPM");

        opt.bpm = 140;
        check (AudioToMidi::transcribeBuffer (audio, 44100, opt, out, err), "transcribed at project tempo");
        if (juce::SystemStats::getEnvironmentVariable ("MIMIC_DEBUG", {}).isNotEmpty())
            for (auto& n : out.notes) std::cout << "    " << n.pitch << " @" << n.start << " v" << n.velocity << std::endl;
        int kicks = 0, snares = 0, hats = 0;
        for (auto& n : out.notes) { kicks += n.pitch == 36; snares += n.pitch == 38; hats += n.pitch == 42; }
        check (kicks >= 9 && kicks <= 11, "kicks: " + juce::String (kicks) + " (expect 10)");
        check (snares >= 7 && snares <= 9, "snares: " + juce::String (snares) + " (expect 8)");
        check (hats >= 26 && hats <= 34, "hats: " + juce::String (hats) + " (expect 32)");
    }

    std::cout << std::endl << (failures == 0 ? "ALL TESTS PASSED" : juce::String (failures) + " FAILURE(S)") << std::endl;
    return failures == 0 ? 0 : 1;
}
