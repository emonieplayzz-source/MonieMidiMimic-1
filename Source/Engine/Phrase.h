#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

namespace mm
{
    /** One note, with times measured in quarter-note beats. */
    struct Note
    {
        int    pitch    = 60;
        double start    = 0.0;   // beats
        double length   = 0.25;  // beats
        float  velocity = 0.8f;  // 0..1
        int    channel  = 1;     // 1..16
    };

    /** A clip of notes plus the timing info needed to play / export it. */
    struct Phrase
    {
        std::vector<Note> notes;
        double lengthBeats = 16.0;
        double bpm         = 120.0;
        int    timeSigNum  = 4;
        int    timeSigDen  = 4;
        bool   isDrums     = false;
        juce::String name;

        bool   empty() const noexcept          { return notes.empty(); }
        double beatsPerBar() const noexcept    { return timeSigNum * 4.0 / (double) juce::jmax (1, timeSigDen); }
        int    numBars() const noexcept        { return juce::jmax (1, (int) std::round (lengthBeats / beatsPerBar())); }

        void sort()
        {
            std::stable_sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b)
            {
                if (std::abs (a.start - b.start) > 1.0e-9) return a.start < b.start;
                return a.pitch < b.pitch;
            });
        }

        /** Sets lengthBeats to the end of the last note, rounded up to a whole bar. */
        void computeLengthFromNotes()
        {
            double end = 0.0;
            for (auto& n : notes)
                end = juce::jmax (end, n.start + n.length);

            const double bar = beatsPerBar();
            // A note that ends a hair past a barline still belongs to the previous bar.
            lengthBeats = juce::jmax (bar, std::ceil ((end - 1.0e-3) / bar) * bar);
        }

        int lowestPitch() const
        {
            int lo = 127;
            for (auto& n : notes) lo = juce::jmin (lo, n.pitch);
            return notes.empty() ? 60 : lo;
        }

        int highestPitch() const
        {
            int hi = 0;
            for (auto& n : notes) hi = juce::jmax (hi, n.pitch);
            return notes.empty() ? 72 : hi;
        }
    };
}
