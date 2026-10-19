#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    constexpr int kBarChoices[] = { 0, 1, 2, 4, 8, 16, 32 };

    // UTF-8 written as bytes so the text is identical whatever code page the compiler uses.
    const juce::String& dot()   { static const auto s = juce::String::fromUTF8 ("  \xc2\xb7  "); return s; }
    const juce::String& arrow() { static const auto s = juce::String::fromUTF8 (" \xe2\x86\x92 "); return s; }
    const juce::String& ellipsis() { static const auto s = juce::String::fromUTF8 ("\xe2\x80\xa6"); return s; }

    bool nameSuggestsDrums (const juce::String& name)
    {
        const auto n = name.toLowerCase();
        for (auto* word : { "drum", "perc", "kick", "snare", "hat", "break", "groove" })
            if (n.contains (word))
                return true;
        return false;
    }
}

//==============================================================================
class MimicProcessor::LoadJob : public juce::Thread
{
public:
    LoadJob (MimicProcessor& p, juce::File f, mm::TranscribeOptions o)
        : juce::Thread ("Mimic audio import"), owner (&p), progress (p.progress), file (std::move (f)), options (o) {}

    void run() override
    {
        mm::Phrase phrase;
        juce::String error;

        const bool ok = mm::AudioToMidi::transcribeFile (file, options, phrase, error, [this] (float p)
        {
            progress.store (p);
            return ! threadShouldExit();
        });

        if (threadShouldExit())
            return;

        auto safe = owner;
        auto f = file;
        juce::MessageManager::callAsync ([safe, ok, phrase, error, f]() mutable
        {
            if (auto* p = safe.get())
            {
                p->busy = false;
                if (ok) p->setSource (std::move (phrase), "audio" + arrow() + "MIDI");
                else    p->setStatus (error, true);
                p->sendChangeMessage();
            }
        });
    }

private:
    juce::WeakReference<MimicProcessor> owner;
    std::atomic<float>& progress;
    juce::File file;
    mm::TranscribeOptions options;
};

//==============================================================================
MimicProcessor::MimicProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "MonieMidiMimic", createLayout())
{
    for (int i = 0; i < 24; ++i)
        synth.addVoice (new PreviewVoice (drumSound));
    synth.addSound (new PreviewSound());

    for (auto* id : { ParamIDs::similarity, ParamIDs::creativity, ParamIDs::density, ParamIDs::humanize,
                      ParamIDs::bars, ParamIDs::transpose, ParamIDs::octave, ParamIDs::lockKey,
                      ParamIDs::keepRhythm, ParamIDs::mode, ParamIDs::listen })
        apvts.addParameterListener (id, this);

    followParam = apvts.getRawParameterValue (ParamIDs::follow);
    soundParam  = apvts.getRawParameterValue (ParamIDs::sound);
    volumeParam = apvts.getRawParameterValue (ParamIDs::volume);

    lastExportFolder = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Monie MIDI Mimic");
    status = "Drop a MIDI or audio file (WAV / MP3 / FLAC) to start.";

    startTimerHz (15);
}

MimicProcessor::~MimicProcessor()
{
    stopTimer();
    if (loadJob != nullptr)
        loadJob->stopThread (5000);
}

juce::AudioProcessorValueTreeState::ParameterLayout MimicProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto pct = [] (const char* id, const char* name, float lo, float def)
    {
        return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (lo, 100.0f, 1.0f), def,
                                                      AudioParameterFloatAttributes().withLabel ("%"));
    };

    layout.add (pct (ParamIDs::similarity, "Similarity", 0.0f, 70.0f));
    layout.add (pct (ParamIDs::creativity, "Creativity", 0.0f, 40.0f));
    layout.add (pct (ParamIDs::density,    "Density",  -100.0f, 0.0f));
    layout.add (pct (ParamIDs::humanize,   "Humanize",    0.0f, 10.0f));

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::bars, 1 }, "Length",
        StringArray { "Same as source", "1 bar", "2 bars", "4 bars", "8 bars", "16 bars", "32 bars" }, 0));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { ParamIDs::transpose, 1 }, "Transpose", -12, 12, 0));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { ParamIDs::octave, 1 }, "Octave", -2, 2, 0));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::lockKey, 1 }, "Lock to key", true));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::keepRhythm, 1 }, "Keep rhythm", false));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::mode, 1 }, "Mode",
        StringArray { "Auto", "Melodic", "Drums" }, 0));

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::listen, 1 }, "Listen",
        StringArray { "Generated", "Source" }, 0));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::sound, 1 }, "Built-in sound", true));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::volume, 1 }, "Volume",
        NormalisableRange<float> (-36.0f, 6.0f, 0.1f), -6.0f, AudioParameterFloatAttributes().withLabel ("dB")));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::follow, 1 }, "Follow FL transport", true));

    layout.add (std::make_unique<AudioParameterInt> (ParameterID { ParamIDs::audioPoly, 1 }, "Notes at once", 1, 8, 4));
    layout.add (pct (ParamIDs::audioSens, "Sensitivity", 0.0f, 50.0f));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::quantize, 1 }, "Quantize", true));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::projectTempo, 1 }, "Use project tempo", true));

    return layout;
}

//==============================================================================
void MimicProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate;
    synth.setCurrentPlaybackSampleRate (sampleRate);
    numActive = 0;
    wasRunning = false;
}

bool MimicProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void MimicProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    const int n = buffer.getNumSamples();

    bool playing = false;
    double ppq = 0.0, bpm = 120.0;
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = juce::jlimit (20.0, 999.0, *b);
            if (auto p = pos->getPpqPosition()) ppq = *p;
            playing = pos->getIsPlaying();
        }
    }
    hostBpm.store (getPlayHead() != nullptr ? bpm : 0.0);
    hostPlaying.store (playing);

    // Live MIDI coming in (e.g. you playing your keyboard) is passed through and audible.
    juce::MidiBuffer out;
    for (const auto meta : midi)
        out.addEvent (meta.getMessage(), meta.samplePosition);

    if (previewRestart.exchange (false))
    {
        previewBeat = 0.0;
        wasRunning = false;
    }

    const bool useHost = playing && followParam->load() > 0.5f;
    const bool usePreview = ! useHost && previewPlaying.load();
    const double beatsPerSample = bpm / 60.0 / currentSampleRate;

    if (useHost || usePreview)
    {
        const double b0 = useHost ? ppq : previewBeat;
        const double b1 = b0 + n * beatsPerSample;

        if (! wasRunning || std::abs (b0 - lastEndBeat) > 0.02)
            allNotesOff (out, 0);  // started, or the playhead jumped

        schedule (b0, b1, 1.0 / beatsPerSample, n, out);

        lastEndBeat = b1;
        wasRunning = true;
        if (usePreview) previewBeat = b1;

        const double len = playLength;
        playheadBeat.store (len > 0 ? std::fmod (juce::jmax (0.0, b0), len) : -1.0);
    }
    else if (wasRunning)
    {
        allNotesOff (out, 0);
        wasRunning = false;
        playheadBeat.store (-1.0);
    }

    synth.renderNextBlock (buffer, out, 0, n);

    const float target = soundParam->load() > 0.5f ? juce::Decibels::decibelsToGain (volumeParam->load()) : 0.0f;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        buffer.applyGainRamp (ch, 0, n, lastGain, target);
    lastGain = target;

    midi.swapWith (out);
}

void MimicProcessor::schedule (double b0, double b1, double samplesPerBeat, int n, juce::MidiBuffer& out)
{
    juce::SpinLock::ScopedTryLockType lock (playLock);
    if (! lock.isLocked())
        return;

    if (playReset)
    {
        allNotesOff (out, 0);
        playReset = false;
    }

    auto toSample = [&] (double beat) { return juce::jlimit (0, n - 1, (int) std::floor ((beat - b0) * samplesPerBeat)); };

    auto releaseDue = [&]
    {
        for (int i = 0; i < numActive;)
        {
            if (active[(size_t) i].endBeat < b1)
            {
                out.addEvent (juce::MidiMessage::noteOff (active[(size_t) i].channel, active[(size_t) i].pitch),
                              toSample (active[(size_t) i].endBeat));
                active[(size_t) i] = active[(size_t) --numActive];
            }
            else ++i;
        }
    };

    releaseDue();

    const double L = playLength;
    if (! playNotes.empty() && L > 0.0)
    {
        const auto k0 = (long long) std::floor (b0 / L);
        const auto k1 = (long long) std::floor (b1 / L);

        for (auto k = k0; k <= k1; ++k)
        {
            const double offset = (double) k * L;
            for (auto& pn : playNotes)
            {
                const double s = pn.start + offset;
                if (s < b0) continue;
                if (s >= b1) break;

                const int pos = toSample (s);
                for (int i = 0; i < numActive; ++i)
                {
                    if (active[(size_t) i].pitch == pn.pitch && active[(size_t) i].channel == pn.channel)
                    {
                        out.addEvent (juce::MidiMessage::noteOff (pn.channel, pn.pitch), pos);
                        active[(size_t) i] = active[(size_t) --numActive];
                        break;
                    }
                }

                out.addEvent (juce::MidiMessage::noteOn (pn.channel, pn.pitch, pn.velocity), pos);
                if (numActive < (int) active.size())
                    active[(size_t) numActive++] = { pn.pitch, pn.channel, s + juce::jmin (pn.length, L) };
            }
        }
    }

    releaseDue();
}

void MimicProcessor::allNotesOff (juce::MidiBuffer& out, int samplePos)
{
    for (int i = 0; i < numActive; ++i)
        out.addEvent (juce::MidiMessage::noteOff (active[(size_t) i].channel, active[(size_t) i].pitch), samplePos);
    numActive = 0;
}

//==============================================================================
void MimicProcessor::parameterChanged (const juce::String& id, float)
{
    if (id == ParamIDs::listen)
    {
        playbackDirty = true;
        return;
    }

    if (id == ParamIDs::mode)
        reanalysePending = true;

    regenPending = true;
}

void MimicProcessor::timerCallback()
{
    if (reanalysePending.exchange (false))
        analysedMode = -1;

    if (regenPending.exchange (false) && hasSource())
        regenerateWithCurrentSeed();

    if (playbackDirty.exchange (false))
        rebuildPlayback();
}

mm::GenParams MimicProcessor::currentParams() const
{
    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };

    mm::GenParams gp;
    gp.similarity = get (ParamIDs::similarity) / 100.0f;
    gp.creativity = get (ParamIDs::creativity) / 100.0f;
    gp.density    = get (ParamIDs::density) / 100.0f;
    gp.humanize   = get (ParamIDs::humanize) / 100.0f;
    gp.bars       = kBarChoices[juce::jlimit (0, 6, (int) get (ParamIDs::bars))];
    gp.transpose  = (int) get (ParamIDs::transpose);
    gp.octave     = (int) get (ParamIDs::octave);
    gp.lockToKey  = get (ParamIDs::lockKey) > 0.5f;
    gp.keepRhythm = get (ParamIDs::keepRhythm) > 0.5f;
    gp.mode       = (mm::StyleMode) juce::jlimit (0, 2, (int) get (ParamIDs::mode));
    gp.seed       = seed;
    return gp;
}

//==============================================================================
bool MimicProcessor::isMidiFile (const juce::File& f)
{
    return f.hasFileExtension ("mid;midi;smf");
}

bool MimicProcessor::isAudioFile (const juce::File& f)
{
    return f.hasFileExtension ("wav;aif;aiff;flac;ogg;mp3");
}

juce::String MimicProcessor::loadableWildcard()
{
    return "*.mid;*.midi;" + mm::AudioToMidi::supportedWildcard();
}

void MimicProcessor::loadFile (const juce::File& file)
{
    if (! file.existsAsFile())
    {
        setStatus ("Couldn't find " + file.getFullPathName(), true);
        return;
    }

    if (isMidiFile (file))
    {
        mm::Phrase p;
        juce::String err;
        if (! mm::MidiIO::load (file, p, err))
        {
            setStatus (err, true);
            return;
        }
        setSource (std::move (p), "MIDI");
        return;
    }

    if (! isAudioFile (file))
    {
        setStatus ("Can't use " + file.getFileName() + ". Load a .mid, .wav, .mp3, .flac, .aiff or .ogg file.", true);
        return;
    }

    if (loadJob != nullptr)
        loadJob->stopThread (5000);

    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    const int mode = (int) get (ParamIDs::mode);

    mm::TranscribeOptions opts;
    opts.drums        = mode == 2 || (mode == 0 && nameSuggestsDrums (file.getFileName()));
    opts.maxPolyphony = (int) get (ParamIDs::audioPoly);
    opts.sensitivity  = get (ParamIDs::audioSens) / 100.0f;
    opts.quantize     = get (ParamIDs::quantize) > 0.5f;
    opts.bpm          = (get (ParamIDs::projectTempo) > 0.5f && hostBpm.load() > 0.0) ? hostBpm.load() : 0.0;

    busy = true;
    progress = 0.0f;
    setStatus (juce::String ("Listening to ") + file.getFileName()
               + (opts.drums ? " as drums" : "") + ellipsis());

    loadJob = std::make_unique<LoadJob> (*this, file, opts);
    loadJob->startThread();
    sendChangeMessage();
}

void MimicProcessor::setSource (mm::Phrase phrase, const juce::String& origin)
{
    const auto name = phrase.name;
    {
        const juce::ScopedLock sl (dataLock);
        source = std::move (phrase);
        sourceOrigin = origin;
        analysedMode = -1;
    }

    seedHistory.clear();
    generate (true);
    setStatus ("Loaded " + (name.isNotEmpty() ? name : juce::String ("clip")) + " (" + origin
               + "). Turn the knobs or hit Generate for a new take.");
    sendChangeMessage();
}

void MimicProcessor::generate (bool newSeed)
{
    if (! hasSource())
    {
        setStatus ("Load a MIDI or audio file first.", true);
        sendChangeMessage();
        return;
    }

    if (newSeed || seedHistory.empty())
    {
        seed = (juce::uint32) juce::Random::getSystemRandom().nextInt (999999) + 1;
        seedHistory.push_back (seed);
        if (seedHistory.size() > 64)
            seedHistory.erase (seedHistory.begin());
    }

    regenerateWithCurrentSeed();
}

void MimicProcessor::goBack()
{
    if (seedHistory.size() < 2)
        return;

    seedHistory.pop_back();
    seed = seedHistory.back();
    regenerateWithCurrentSeed();
}

void MimicProcessor::regenerateWithCurrentSeed()
{
    {
        const juce::ScopedLock sl (dataLock);
        if (source.empty())
            return;

        const int mode = (int) apvts.getRawParameterValue (ParamIDs::mode)->load();
        if (analysedMode != mode || ! model.isReady())
        {
            model.analyse (source, (mm::StyleMode) mode);
            analysedMode = mode;
        }

        auto out = model.generate (currentParams());
        out.name = (source.name.isNotEmpty() ? source.name : juce::String ("clip")) + " mimic";
        lastSimilarity = mm::similarityScore (source, out);
        generated = std::move (out);
    }

    rebuildPlayback();
    sendChangeMessage();
}

void MimicProcessor::useGeneratedAsSource()
{
    auto g = getGenerated();
    if (g.empty())
        return;

    g.name = g.name.upToLastOccurrenceOf (" mimic", false, false) + " v2";
    setSource (std::move (g), "previous take");
}

void MimicProcessor::rebuildPlayback()
{
    const auto p = isListeningToSource() ? getSource() : getGenerated();

    std::vector<PlayNote> notes;
    notes.reserve (p.notes.size());
    for (auto& n : p.notes)
        notes.push_back ({ n.start, n.length, n.pitch, juce::jlimit (1, 16, n.channel), n.velocity });

    std::stable_sort (notes.begin(), notes.end(), [] (auto& a, auto& b) { return a.start < b.start; });
    drumSound = p.isDrums;

    {
        const juce::SpinLock::ScopedLockType sl (playLock);
        playNotes.swap (notes);
        playLength = juce::jmax (p.beatsPerBar(), p.lengthBeats);
        playReset = true;
    }
    // 'notes' now holds the old list and is freed here, on the message thread.
}

void MimicProcessor::setStatus (const juce::String& text, bool isError)
{
    status = text;
    statusError = isError;
}

bool MimicProcessor::isListeningToSource() const
{
    return apvts.getRawParameterValue (ParamIDs::listen)->load() > 0.5f;
}

void MimicProcessor::setPreviewPlaying (bool shouldPlay)
{
    if (shouldPlay && ! previewPlaying.load())
        previewRestart = true;
    previewPlaying = shouldPlay;
}

//==============================================================================
mm::Phrase MimicProcessor::getSource() const    { const juce::ScopedLock sl (dataLock); return source; }
mm::Phrase MimicProcessor::getGenerated() const { const juce::ScopedLock sl (dataLock); return generated; }
bool MimicProcessor::hasSource() const          { const juce::ScopedLock sl (dataLock); return ! source.empty(); }
bool MimicProcessor::hasGenerated() const       { const juce::ScopedLock sl (dataLock); return ! generated.empty(); }

juce::String MimicProcessor::getSourceInfo() const
{
    const juce::ScopedLock sl (dataLock);
    if (source.empty())
        return "No source loaded";

    juce::String s;
    s << (source.name.isNotEmpty() ? source.name : juce::String ("clip")) << dot();
    if (model.isReady() && model.isDrums()) s << "drums";
    else                                     s << mm::detectKey (source).name();
    s << dot() << juce::String (source.bpm, 0) << " BPM" << dot() << source.numBars()
      << (source.numBars() == 1 ? " bar" : " bars") << dot() << (int) source.notes.size() << " notes"
      << dot() << "from " << sourceOrigin;
    return s;
}

juce::String MimicProcessor::getGeneratedInfo() const
{
    const juce::ScopedLock sl (dataLock);
    if (generated.empty())
        return "Nothing generated yet";

    juce::String s;
    s << "Take #" << (int) seed << dot() << generated.numBars() << (generated.numBars() == 1 ? " bar" : " bars")
      << dot() << (int) generated.notes.size() << " notes" << dot()
      << juce::roundToInt (lastSimilarity * 100.0) << "% match to source";
    return s;
}

juce::String MimicProcessor::suggestedFileName (bool generatedOne) const
{
    const juce::ScopedLock sl (dataLock);
    const auto& p = generatedOne ? generated : source;
    auto base = p.name.isNotEmpty() ? p.name : juce::String (generatedOne ? "mimic" : "source");
    if (generatedOne) base << " " << (int) seed;
    return juce::File::createLegalFileName (base) + ".mid";
}

bool MimicProcessor::exportPhrase (bool generatedOne, const juce::File& file, juce::String& error)
{
    const auto p = generatedOne ? getGenerated() : getSource();
    if (p.empty())
    {
        error = "There's nothing to export yet.";
        return false;
    }

    auto target = file.hasFileExtension ("mid;midi") ? file : file.withFileExtension ("mid");
    if (! mm::MidiIO::save (p, target, error))
        return false;

    lastExportFolder = target.getParentDirectory();
    setStatus ("Saved " + target.getFullPathName());
    sendChangeMessage();
    return true;
}

juce::File MimicProcessor::writeTempMidiForDrag()
{
    const auto p = getGenerated();
    if (p.empty())
        return {};

    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Monie MIDI Mimic");
    dir.createDirectory();

    auto file = dir.getChildFile (suggestedFileName (true));
    juce::String err;
    if (! mm::MidiIO::save (p, file, err))
    {
        setStatus (err, true);
        sendChangeMessage();
        return {};
    }
    return file;
}

//==============================================================================
void MimicProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    if (xml == nullptr)
        return;

    auto* data = xml->createNewChildElement ("MimicData");
    {
        const juce::ScopedLock sl (dataLock);
        if (! source.empty())
        {
            data->setAttribute ("source", mm::MidiIO::toMemory (source).toBase64Encoding());
            data->setAttribute ("sourceName", source.name);
            data->setAttribute ("sourceDrums", source.isDrums);
        }
        if (! generated.empty())
        {
            data->setAttribute ("generated", mm::MidiIO::toMemory (generated).toBase64Encoding());
            data->setAttribute ("generatedName", generated.name);
            data->setAttribute ("generatedDrums", generated.isDrums);
        }
        data->setAttribute ("origin", sourceOrigin);
        data->setAttribute ("seed", (int) seed);
        data->setAttribute ("exportFolder", lastExportFolder.getFullPathName());
    }

    copyXmlToBinary (*xml, destData);
}

void MimicProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    mm::Phrase src, gen;
    juce::String origin = "MIDI";
    juce::uint32 savedSeed = 1;

    if (auto* d = xml->getChildByName ("MimicData"))
    {
        juce::MemoryBlock mb;
        if (mb.fromBase64Encoding (d->getStringAttribute ("source")) && mm::MidiIO::fromMemory (mb, src))
        {
            src.name = d->getStringAttribute ("sourceName");
            src.isDrums = d->getBoolAttribute ("sourceDrums", src.isDrums);
        }
        juce::MemoryBlock gb;
        if (gb.fromBase64Encoding (d->getStringAttribute ("generated")) && mm::MidiIO::fromMemory (gb, gen))
        {
            gen.name = d->getStringAttribute ("generatedName");
            gen.isDrums = d->getBoolAttribute ("generatedDrums", gen.isDrums);
        }
        origin = d->getStringAttribute ("origin", origin);
        savedSeed = (juce::uint32) juce::jmax (1, d->getIntAttribute ("seed", 1));

        const auto folder = d->getStringAttribute ("exportFolder");
        if (juce::File::isAbsolutePath (folder))
            lastExportFolder = juce::File (folder);

        xml->removeChildElement (d, true);
    }

    apvts.replaceState (juce::ValueTree::fromXml (*xml));

    {
        const juce::ScopedLock sl (dataLock);
        source = std::move (src);
        generated = std::move (gen);
        sourceOrigin = origin;
        seed = savedSeed;
        analysedMode = -1;
        if (! source.empty())
        {
            const int mode = (int) apvts.getRawParameterValue (ParamIDs::mode)->load();
            model.analyse (source, (mm::StyleMode) mode);
            analysedMode = mode;
            if (! generated.empty())
                lastSimilarity = mm::similarityScore (source, generated);
        }
    }

    seedHistory = { seed };
    regenPending = false;   // keep the saved take exactly as it was
    reanalysePending = false;

    if (hasSource())
    {
        setStatus ("Restored your last session.");
        if (! hasGenerated())
        {
            regenerateWithCurrentSeed();
            return;
        }
    }

    rebuildPlayback();
    sendChangeMessage();
}

//==============================================================================
juce::AudioProcessorEditor* MimicProcessor::createEditor()
{
    return new MimicEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MimicProcessor();
}
