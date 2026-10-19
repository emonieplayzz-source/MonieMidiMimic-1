#pragma once

#include "Phrase.h"

namespace mm::MidiIO
{
    /** Reads a Standard MIDI File (type 0 or 1). All tracks are merged into one phrase.
        The phrase is shifted so it starts on the bar that contains its first note. */
    bool load (juce::InputStream& in, Phrase& out, juce::String& error);
    bool load (const juce::File& file, Phrase& out, juce::String& error);

    /** Builds a type-1 MIDI file with a tempo/time-signature track and one note track. */
    juce::MidiFile toMidiFile (const Phrase& phrase, int ticksPerQuarter = 960);

    bool save (const Phrase& phrase, const juce::File& file, juce::String& error);

    /** In-memory round trip, used for saving the plugin state inside the FL project. */
    juce::MemoryBlock toMemory (const Phrase& phrase);
    bool fromMemory (const juce::MemoryBlock& data, Phrase& out);

    /** True for General-MIDI style drum clips (mostly channel 10). */
    bool looksLikeDrums (const Phrase& phrase);
}
