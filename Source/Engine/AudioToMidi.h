#pragma once

#include "Phrase.h"
#include <functional>

namespace mm
{
    struct TranscribeOptions
    {
        bool   drums        = false;  // true: kick / snare / hat detection instead of pitches
        int    maxPolyphony = 4;      // 1 = melody or bass line only
        float  sensitivity  = 0.5f;   // 0..1, higher catches quieter notes (and more junk)
        double bpm          = 0.0;    // > 0 = use this tempo (e.g. the FL project tempo), 0 = detect
        bool   quantize     = true;   // snap to a 16th grid
    };

    /** Turns audio into a MIDI phrase. This is an offline, best-effort transcriber:
        it works well on clean loops, single instruments and drum loops, and gives a
        rougher sketch on full mixes. */
    namespace AudioToMidi
    {
        using Progress = std::function<bool (float)>; // return false to cancel

        bool transcribeFile (const juce::File& file, const TranscribeOptions& options,
                             Phrase& out, juce::String& error, Progress progress = {});

        bool transcribeBuffer (const juce::AudioBuffer<float>& audio, double sampleRate,
                               const TranscribeOptions& options, Phrase& out,
                               juce::String& error, Progress progress = {});

        /** Tempo from an onset-strength envelope sampled at framesPerSecond. */
        double estimateTempo (const std::vector<float>& onsetEnvelope, double framesPerSecond);

        juce::String supportedWildcard();
    }
}
