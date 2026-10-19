#include "MidiIO.h"

namespace mm::MidiIO
{
    bool looksLikeDrums (const Phrase& phrase)
    {
        if (phrase.notes.empty())
            return false;

        int ch10 = 0;
        for (auto& n : phrase.notes)
            if (n.channel == 10)
                ++ch10;

        return ch10 * 2 > (int) phrase.notes.size();
    }

    bool load (juce::InputStream& in, Phrase& out, juce::String& error)
    {
        juce::MidiFile mf;
        int fileType = 0;

        if (! mf.readFrom (in, true, &fileType))
        {
            error = "That file isn't a readable MIDI file.";
            return false;
        }

        Phrase p;
        const short timeFormat = mf.getTimeFormat();
        const bool  usesTicks  = timeFormat > 0;
        bool foundTempo = false, foundSig = false;

        // Tempo and time signature: the first of each wins.
        for (int t = 0; t < mf.getNumTracks(); ++t)
        {
            auto* track = mf.getTrack (t);
            for (int i = 0; i < track->getNumEvents(); ++i)
            {
                auto& msg = track->getEventPointer (i)->message;

                if (! foundTempo && msg.isTempoMetaEvent())
                {
                    const double spq = msg.getTempoSecondsPerQuarterNote();
                    if (spq > 0.0)
                    {
                        p.bpm = juce::jlimit (20.0, 400.0, 60.0 / spq);
                        foundTempo = true;
                    }
                }
                else if (! foundSig && msg.isTimeSignatureMetaEvent())
                {
                    int num = 4, den = 4;
                    msg.getTimeSignatureInfo (num, den);
                    if (num > 0 && den > 0)
                    {
                        p.timeSigNum = num;
                        p.timeSigDen = den;
                        foundSig = true;
                    }
                }
            }
        }

        if (! usesTicks)
            mf.convertTimestampTicksToSeconds();

        auto toBeats = [&] (double stamp)
        {
            return usesTicks ? stamp / (double) timeFormat
                             : stamp * p.bpm / 60.0;
        };

        for (int t = 0; t < mf.getNumTracks(); ++t)
        {
            juce::MidiMessageSequence seq (*mf.getTrack (t));
            seq.updateMatchedPairs();

            for (int i = 0; i < seq.getNumEvents(); ++i)
            {
                auto* ev = seq.getEventPointer (i);
                if (! ev->message.isNoteOn())
                    continue;

                Note n;
                n.pitch    = ev->message.getNoteNumber();
                n.channel  = ev->message.getChannel();
                n.velocity = juce::jlimit (0.05f, 1.0f, ev->message.getFloatVelocity());
                n.start    = toBeats (ev->message.getTimeStamp());

                if (ev->noteOffObject != nullptr)
                    n.length = toBeats (ev->noteOffObject->message.getTimeStamp()) - n.start;
                else
                    n.length = 0.25;

                n.length = juce::jmax (1.0 / 64.0, n.length);
                p.notes.push_back (n);
            }
        }

        if (p.notes.empty())
        {
            error = "That MIDI file has no notes in it.";
            return false;
        }

        p.sort();

        // Shift so the clip starts at the bar holding the first note.
        const double bar = p.beatsPerBar();
        const double offset = std::floor (p.notes.front().start / bar + 1.0e-6) * bar;
        if (offset > 0.0)
            for (auto& n : p.notes)
                n.start -= offset;

        p.computeLengthFromNotes();

        // Respect deliberate empty bars at the end (the file's end-of-track marker).
        double fileEnd = 0.0;
        for (int t = 0; t < mf.getNumTracks(); ++t)
            fileEnd = juce::jmax (fileEnd, toBeats (mf.getTrack (t)->getEndTime()));

        const double fileBars = std::round ((fileEnd - offset) / bar);
        if (fileBars * bar > p.lengthBeats && fileBars <= 512)
            p.lengthBeats = fileBars * bar;

        p.isDrums = looksLikeDrums (p);
        out = std::move (p);
        return true;
    }

    bool load (const juce::File& file, Phrase& out, juce::String& error)
    {
        juce::FileInputStream in (file);
        if (! in.openedOk())
        {
            error = "Couldn't open " + file.getFileName();
            return false;
        }

        if (! load (in, out, error))
            return false;

        out.name = file.getFileNameWithoutExtension();
        return true;
    }

    juce::MidiFile toMidiFile (const Phrase& phrase, int ppq)
    {
        auto toTicks = [ppq] (double beats) { return std::round (beats * ppq); };

        juce::MidiMessageSequence meta;
        auto tempoMsg = juce::MidiMessage::tempoMetaEvent ((int) std::round (60000000.0 / juce::jmax (1.0, phrase.bpm)));
        tempoMsg.setTimeStamp (0);
        meta.addEvent (tempoMsg);

        auto sigMsg = juce::MidiMessage::timeSignatureMetaEvent (phrase.timeSigNum, phrase.timeSigDen);
        sigMsg.setTimeStamp (0);
        meta.addEvent (sigMsg);

        auto endMeta = juce::MidiMessage::endOfTrack();
        endMeta.setTimeStamp (toTicks (phrase.lengthBeats));
        meta.addEvent (endMeta);

        juce::MidiMessageSequence track;
        auto nameMsg = juce::MidiMessage::textMetaEvent (3, phrase.name.isNotEmpty() ? phrase.name : juce::String ("Monie MIDI Mimic"));
        nameMsg.setTimeStamp (0);
        track.addEvent (nameMsg);

        // Note-offs go in first so that, at equal timestamps, an off sorts before the next on.
        for (auto& n : phrase.notes)
        {
            const double on  = toTicks (n.start);
            const double off = juce::jmax (on + 1.0, toTicks (n.start + n.length));
            auto m = juce::MidiMessage::noteOff (juce::jlimit (1, 16, n.channel), juce::jlimit (0, 127, n.pitch));
            m.setTimeStamp (off);
            track.addEvent (m);
        }

        for (auto& n : phrase.notes)
        {
            auto vel = (juce::uint8) juce::jlimit (1, 127, (int) std::round (n.velocity * 127.0f));
            auto m = juce::MidiMessage::noteOn (juce::jlimit (1, 16, n.channel), juce::jlimit (0, 127, n.pitch), vel);
            m.setTimeStamp (toTicks (n.start));
            track.addEvent (m);
        }

        track.updateMatchedPairs();

        auto endTrack = juce::MidiMessage::endOfTrack();
        endTrack.setTimeStamp (juce::jmax (toTicks (phrase.lengthBeats), track.getEndTime()));
        track.addEvent (endTrack);

        juce::MidiFile mf;
        mf.setTicksPerQuarterNote (ppq);
        mf.addTrack (meta);
        mf.addTrack (track);
        return mf;
    }

    bool save (const Phrase& phrase, const juce::File& file, juce::String& error)
    {
        auto mf = toMidiFile (phrase);

        juce::TemporaryFile temp (file);
        {
            juce::FileOutputStream out (temp.getFile());
            if (! out.openedOk() || ! mf.writeTo (out, 1))
            {
                error = "Couldn't write " + file.getFullPathName();
                return false;
            }
        }

        if (! temp.overwriteTargetFileWithTemporary())
        {
            error = "Couldn't save to " + file.getFullPathName() + " (is it open somewhere else?)";
            return false;
        }
        return true;
    }

    juce::MemoryBlock toMemory (const Phrase& phrase)
    {
        juce::MemoryOutputStream out;
        toMidiFile (phrase).writeTo (out, 1);
        return out.getMemoryBlock();
    }

    bool fromMemory (const juce::MemoryBlock& data, Phrase& out)
    {
        if (data.getSize() == 0)
            return false;

        juce::MemoryInputStream in (data, false);
        juce::String err;
        Phrase p;
        if (! load (in, p, err))
            return false;

        out = std::move (p);
        return true;
    }
}
