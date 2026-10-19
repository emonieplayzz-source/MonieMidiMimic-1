// App-level smoke test. Part A loads the built VST3 binary through JUCE's VST3 host and
// runs it. Part B drives the real processor + editor code the way FL Studio would:
// load files, play with a moving song position, tweak knobs, export, save/restore state,
// convert audio, and render the UI to a PNG.
//
// Usage: MimicHostTest <path to "Monie MIDI Mimic.vst3"> <output.png>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "../Source/PluginProcessor.h"

#include <iostream>

static int failures = 0;
static void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  [ok]   " : "  [FAIL] ") << what << std::endl;
    if (! ok) ++failures;
}

struct FakePlayHead : juce::AudioPlayHead
{
    double ppq = 0.0, bpm = 140.0;
    bool playing = true;

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setBpm (bpm);
        p.setPpqPosition (ppq);
        p.setIsPlaying (playing);
        p.setTimeInSamples ((juce::int64) (ppq * 60.0 / bpm * 44100.0));
        p.setTimeSignature (TimeSignature { 4, 4 });
        return p;
    }
};

struct RunStats { int noteOns = 0, noteOffs = 0; double rms = 0.0; };

static RunStats runBlocks (juce::AudioProcessor& proc, FakePlayHead& ph, int blocks, int blockSize = 512)
{
    RunStats st;
    juce::AudioBuffer<float> buffer (2, blockSize);
    double sumSq = 0.0; long long count = 0;

    for (int b = 0; b < blocks; ++b)
    {
        juce::MidiBuffer midi;
        buffer.clear();
        proc.processBlock (buffer, midi);
        for (const auto m : midi)
        {
            if (m.getMessage().isNoteOn()) ++st.noteOns;
            else if (m.getMessage().isNoteOff()) ++st.noteOffs;
        }
        for (int i = 0; i < blockSize; ++i) { sumSq += buffer.getSample (0, i) * buffer.getSample (0, i); ++count; }
        if (ph.playing)
            ph.ppq += blockSize * ph.bpm / 60.0 / 44100.0;
    }
    st.rms = std::sqrt (sumSq / juce::jmax (1LL, count));
    return st;
}

static juce::File writeMelodicMidi (const juce::File& dir)
{
    mm::Phrase p;
    p.bpm = 140;
    p.name = "my melody";
    const int chords[4][3] = { { 57, 60, 64 }, { 53, 57, 60 }, { 48, 52, 55 }, { 55, 59, 62 } };
    const int melody[16]   = { 76, 74, 72, 74, 72, 69, 72, 76, 79, 76, 74, 72, 74, 71, 67, 71 };
    for (int bar = 0; bar < 4; ++bar)
    {
        for (int k = 0; k < 3; ++k) p.notes.push_back ({ chords[bar][k], bar * 4.0, 3.5, 0.7f, 1 });
        for (int i = 0; i < 4; ++i)
            p.notes.push_back ({ melody[bar * 4 + i], bar * 4.0 + (i < 2 ? i * 0.75 : 1.5 + (i - 2)), 0.5, 0.9f, 1 });
    }
    p.sort();
    p.computeLengthFromNotes();
    auto f = dir.getChildFile ("my melody.mid");
    juce::String err;
    mm::MidiIO::save (p, f, err);
    return f;
}

static juce::File writeDrumWav (const juce::File& dir)
{
    const double sr = 44100.0, bpm = 140.0;
    juce::AudioBuffer<float> buf (2, (int) (sr * 16 * 60.0 / bpm + sr * 0.5));
    buf.clear();
    juce::Random r (9);
    juce::IIRFilter hp1, hp2;
    hp1.setCoefficients (juce::IIRCoefficients::makeHighPass (sr, 7000.0));
    hp2.setCoefficients (juce::IIRCoefficients::makeHighPass (sr, 7000.0));

    for (int step = 0; step < 64; ++step)
    {
        const int s0 = (int) (step * 0.25 * 60.0 / bpm * sr);
        const int bar16 = step % 16;
        const bool kick = bar16 == 0 || bar16 == 10, snare = bar16 == 4 || bar16 == 12, hat = step % 2 == 0;
        double ph = 0;
        for (int i = 0; i < (int) (0.2 * sr) && s0 + i < buf.getNumSamples(); ++i)
        {
            const double t = i / sr;
            float v = 0.0f;
            if (kick)  { ph += juce::MathConstants<double>::twoPi * (50.0 + 110.0 * std::exp (-t * 30.0)) / sr; v += (float) (std::sin (ph) * std::exp (-t * 9.0)); }
            if (snare) v += (float) ((r.nextFloat() * 2 - 1) * 0.7 * std::exp (-t * 18.0));
            if (hat)   v += (float) (0.6 * hp2.processSingleSampleRaw (hp1.processSingleSampleRaw (r.nextFloat() * 2 - 1)) * std::exp (-t * 60.0));
            buf.addSample (0, s0 + i, v * 0.4f);
            buf.addSample (1, s0 + i, v * 0.4f);
        }
    }

    auto f = dir.getChildFile ("trap drum loop.wav");
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
    if (auto* w = wav.createWriterFor (os.get(), sr, 2, 24, {}, 0))
    {
        os.release();
        std::unique_ptr<juce::AudioFormatWriter> writer (w);
        writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
    }
    return f;
}

static void pump (int ms)
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("mimic-hosttest");
    dir.deleteRecursively();
    dir.createDirectory();

    // ---------------------------------------------------------------- Part A
    if (argc > 1)
    {
        std::cout << "A. Load the built VST3 binary" << std::endl;
        juce::VST3PluginFormat fmt;
        juce::OwnedArray<juce::PluginDescription> types;
        fmt.findAllTypesForFile (types, juce::String (argv[1]));
        check (types.size() == 1, "VST3 scanned: " + (types.isEmpty() ? juce::String ("nothing") : types[0]->name + " by " + types[0]->manufacturerName));

        if (! types.isEmpty())
        {
            check (types[0]->isInstrument, "registers as an instrument (category " + types[0]->category + ")");
            juce::String err;
            auto inst = fmt.createInstanceFromDescription (*types[0], 44100.0, 512, err);
            check (inst != nullptr, "instantiated " + err);
            if (inst != nullptr)
            {
                FakePlayHead ph;
                inst->setPlayHead (&ph);
                inst->prepareToPlay (44100.0, 512);
                auto st = runBlocks (*inst, ph, 50);
                check (st.noteOns == 0, "runs silently with nothing loaded");
                check (inst->getParameters().size() >= 10, "exposes " + juce::String (inst->getParameters().size()) + " automatable parameters");
                juce::MemoryBlock state;
                inst->getStateInformation (state);
                check (state.getSize() > 0, "saves state (" + juce::String ((int) state.getSize()) + " bytes)");
                inst->releaseResources();
            }
        }
    }

    // ---------------------------------------------------------------- Part B
    std::cout << "B. Processor behaviour" << std::endl;
    MimicProcessor proc;
    FakePlayHead ph;
    proc.setPlayHead (&ph);
    proc.prepareToPlay (44100.0, 512);

    const auto midiFile = writeMelodicMidi (dir);
    proc.loadFile (midiFile);
    check (proc.hasSource() && proc.hasGenerated(), "MIDI loaded and a take generated");
    std::cout << "         source:    " << proc.getSourceInfo() << std::endl;
    std::cout << "         generated: " << proc.getGeneratedInfo() << std::endl;

    auto st = runBlocks (proc, ph, 200);   // ~2.3 s at 140 BPM ~ 5 beats
    check (st.noteOns > 5, "plays MIDI out in sync with the song position (" + juce::String (st.noteOns) + " note-ons)");
    check (st.rms > 0.001, "built-in sound is audible (rms " + juce::String (st.rms, 4) + ")");

    ph.playing = false;
    st = runBlocks (proc, ph, 5);
    check (st.noteOffs > 0 || st.noteOns == 0, "stopping FL's transport releases notes");
    st = runBlocks (proc, ph, 20);
    check (st.noteOns == 0, "silent while stopped");

    proc.setPreviewPlaying (true);
    st = runBlocks (proc, ph, 200);
    check (st.noteOns > 5, "Play button previews without FL's transport (" + juce::String (st.noteOns) + " note-ons)");
    proc.setPreviewPlaying (false);
    runBlocks (proc, ph, 4);

    const auto before = proc.getGenerated();
    proc.apvts.getParameter (ParamIDs::similarity)->setValueNotifyingHost (0.15f);
    proc.apvts.getParameter (ParamIDs::creativity)->setValueNotifyingHost (0.8f);
    pump (300);
    const auto after = proc.getGenerated();
    bool changed = before.notes.size() != after.notes.size();
    for (size_t i = 0; ! changed && i < before.notes.size(); ++i)
        changed = before.notes[i].pitch != after.notes[i].pitch || std::abs (before.notes[i].start - after.notes[i].start) > 1e-6;
    check (changed, "turning knobs re-generates live");
    std::cout << "         at 15% similarity: " << proc.getGeneratedInfo() << std::endl;

    proc.apvts.getParameter (ParamIDs::bars)->setValueNotifyingHost (proc.apvts.getParameter (ParamIDs::bars)->convertTo0to1 (4.0f)); // "8 bars"
    pump (300);
    check (proc.getGenerated().numBars() == 8, "length switch: " + juce::String (proc.getGenerated().numBars()) + " bars");

    const auto takeA = proc.getGenerated();
    proc.generate (true);
    const auto takeB = proc.getGenerated();
    proc.goBack();
    const auto takeA2 = proc.getGenerated();
    check (takeA2.notes.size() == takeA.notes.size(), "Back returns to the previous take");
    juce::ignoreUnused (takeB);

    juce::String err;
    auto out = dir.getChildFile ("exported.mid");
    check (proc.exportPhrase (true, out, err), "export .mid " + err);
    mm::Phrase reloaded;
    check (mm::MidiIO::load (out, reloaded, err) && reloaded.notes.size() == proc.getGenerated().notes.size(),
           "exported file reloads with the same notes (" + juce::String ((int) reloaded.notes.size()) + ")");
    check (proc.writeTempMidiForDrag().existsAsFile(), "drag-to-FL temp file written");

    juce::MemoryBlock state;
    proc.getStateInformation (state);
    {
        MimicProcessor restored;
        restored.setStateInformation (state.getData(), (int) state.getSize());
        const auto g1 = proc.getGenerated(), g2 = restored.getGenerated();
        bool same = g1.notes.size() == g2.notes.size() && g1.notes.size() > 0;
        for (size_t i = 0; same && i < g1.notes.size(); ++i)
            same = g1.notes[i].pitch == g2.notes[i].pitch && std::abs (g1.notes[i].start - g2.notes[i].start) < 1e-3;
        check (same, "saving the FL project restores the exact same take");
        check (restored.getSourceInfo() == proc.getSourceInfo(), "source restored: " + restored.getSourceInfo());
    }

    std::cout << "C. Audio import" << std::endl;
    const auto wav = writeDrumWav (dir);
    proc.apvts.getParameter (ParamIDs::similarity)->setValueNotifyingHost (0.7f);
    proc.apvts.getParameter (ParamIDs::bars)->setValueNotifyingHost (0.0f);
    pump (100);
    ph.bpm = 140.0;
    runBlocks (proc, ph, 1);   // lets the processor see the project tempo
    proc.loadFile (wav);
    check (proc.isBusy(), "audio conversion runs in the background");
    for (int i = 0; i < 200 && proc.isBusy(); ++i) pump (50);
    check (! proc.isBusy() && proc.getSource().isDrums, "drum loop converted to drum MIDI");
    std::cout << "         status:    " << proc.getStatus() << std::endl;
    std::cout << "         source:    " << proc.getSourceInfo() << std::endl;
    std::cout << "         generated: " << proc.getGeneratedInfo() << std::endl;
    int kicks = 0, snares = 0, hats = 0;
    for (auto& n : proc.getSource().notes) { kicks += n.pitch == 36; snares += n.pitch == 38; hats += n.pitch == 42; }
    if (juce::SystemStats::getEnvironmentVariable ("MIMIC_DEBUG", {}).isNotEmpty())
    {
        juce::String hatSteps;
        for (auto& n : proc.getSource().notes) if (n.pitch == 42) hatSteps << juce::String (n.start * 4.0, 0) << " ";
        std::cout << "         hat steps: " << hatSteps << std::endl;
    }
    check (kicks == 8 && snares == 8 && hats >= 30, "hits: " + juce::String (kicks) + " kicks, " + juce::String (snares)
                                                   + " snares, " + juce::String (hats) + " hats (expected 8 / 8 / 32)");

    // ---------------------------------------------------------------- UI
    std::cout << "D. Editor" << std::endl;
    proc.loadFile (midiFile);
    proc.apvts.getParameter (ParamIDs::bars)->setValueNotifyingHost (proc.apvts.getParameter (ParamIDs::bars)->convertTo0to1 (3.0f)); // 4 bars
    pump (300);
    ph.playing = true; ph.ppq = 6.3;
    runBlocks (proc, ph, 2);
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
        check (editor != nullptr && editor->getWidth() == 1000, "editor opens at " + juce::String (editor->getWidth()) + "x" + juce::String (editor->getHeight()));
        pump (200);
        auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
        if (argc > 2)
        {
            juce::File png { juce::String (argv[2]) };
            png.deleteFile();
            juce::FileOutputStream os (png);
            juce::PNGImageFormat fmt;
            check (fmt.writeImageToStream (img, os), "UI rendered to " + png.getFileName());
        }

        // Drum view too
        proc.loadFile (wav);
        for (int i = 0; i < 200 && proc.isBusy(); ++i) pump (50);
        pump (200);
        if (argc > 2)
        {
            auto img2 = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
            juce::File png2 (juce::File (juce::String (argv[2])).getSiblingFile ("ui-drums.png"));
            png2.deleteFile();
            juce::FileOutputStream os (png2);
            juce::PNGImageFormat fmt;
            check (fmt.writeImageToStream (img2, os), "drum UI rendered to " + png2.getFileName());
        }
    }

    std::cout << std::endl << (failures == 0 ? "ALL TESTS PASSED" : juce::String (failures) + " FAILURE(S)") << std::endl;
    return failures == 0 ? 0 : 1;
}
