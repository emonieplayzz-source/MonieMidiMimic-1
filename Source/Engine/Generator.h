#pragma once

#include "Phrase.h"
#include <array>
#include <map>

namespace mm
{
    enum class StyleMode { Auto = 0, Melodic = 1, Drums = 2 };

    struct GenParams
    {
        float similarity = 0.7f;   // 0 = loosely inspired, 1 = almost identical
        float creativity = 0.4f;   // randomness / temperature
        float density    = 0.0f;   // -1 = sparser, +1 = busier
        float humanize   = 0.1f;   // timing + velocity looseness
        int   bars       = 0;      // 0 = same length as the source
        int   transpose  = 0;      // semitones
        int   octave     = 0;      // octaves
        bool  lockToKey  = true;
        bool  keepRhythm = false;
        StyleMode mode   = StyleMode::Auto;
        juce::uint32 seed = 1;
    };

    struct KeyInfo
    {
        int    root = 0;        // 0 = C
        bool   minor = false;
        double confidence = 0.0;

        std::array<bool, 12> scale() const;
        juce::String name() const;
    };

    KeyInfo detectKey (const Phrase& phrase);

    /** 0..1 score of how alike two phrases are (rhythm, pitch content, exact note matches). */
    double similarityScore (const Phrase& source, const Phrase& generated);

    /** Learns the feel of a source clip (key, groove, melodic motion, chord shapes, drum patterns)
        and writes new clips in that style. Analyse once, generate as often as you like. */
    class StyleModel
    {
    public:
        void analyse (const Phrase& source, StyleMode mode);
        bool isReady() const noexcept    { return ready; }
        bool isDrums() const noexcept    { return drums; }
        const KeyInfo& getKey() const    { return key; }
        double getGrid() const noexcept  { return grid; }

        Phrase generate (const GenParams& params) const;

    private:
        struct SrcNote  { int pitch; double length; float velocity; };
        struct SrcEvent { double start; int step; int bar; std::vector<SrcNote> notes; int anchor; std::vector<int> shape; float velocity; };

        struct OutEvent
        {
            double start = 0;           // beats from the start of its bar
            double length = 0.25;
            std::vector<int> pitches;
            float velocity = 0.8f;
            int  templateIndex = -1;    // index into srcEvents, or -1
            bool keepPitch = false;
        };

        Phrase source;
        bool ready = false, drums = false;
        KeyInfo key;
        std::array<bool, 12> scale {};
        double grid = 0.25, beatsPerBar = 4.0;
        int stepsPerBar = 16, srcBars = 1, channel = 1;
        double avgPolyphony = 1.0, legato = 1.0;
        float  avgVelocity = 0.8f, velocitySpread = 0.1f;
        int lowPitch = 48, highPitch = 84;

        std::vector<SrcEvent> srcEvents;
        std::vector<std::vector<int>> eventsByBar;
        std::vector<juce::String> barSignature;
        std::vector<double> onsetProb, velByStep;
        std::array<double, 12> pcProb {};
        std::map<int, int> durationSteps;
        std::map<std::vector<int>, int> shapeCounts;
        std::map<std::pair<int, int>, std::map<int, int>> interval2;
        std::map<int, std::map<int, int>> interval1;
        std::map<int, int> interval0;

        // drums: pitch -> per-step hit probability / velocity, and per-bar hits
        std::map<int, std::vector<double>> drumProb, drumVel;
        std::vector<std::map<int, std::vector<float>>> drumBars; // [bar][pitch][step] velocity or 0
        int hatPitch = 42;

        void analyseMelodic();
        void analyseDrums();
        Phrase generateMelodic (const GenParams&) const;
        Phrase generateDrums (const GenParams&) const;

        int  pickAnchor (juce::Random&, int prev, int prevInterval, int prevInterval2, float creativity,
                         bool lock, int target, float pull) const;
        std::vector<int> pickShape (juce::Random&, float creativity) const;
        int  snap (int pitch, int preferDir) const;
        int  sampleDurationSteps (juce::Random&) const;
    };
}
