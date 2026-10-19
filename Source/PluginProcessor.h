#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include "Engine/Phrase.h"
#include "Engine/MidiIO.h"
#include "Engine/Generator.h"
#include "Engine/AudioToMidi.h"
#include "PreviewSynth.h"

namespace ParamIDs
{
    inline constexpr auto similarity   = "similarity";
    inline constexpr auto creativity   = "creativity";
    inline constexpr auto density      = "density";
    inline constexpr auto humanize     = "humanize";
    inline constexpr auto bars         = "bars";
    inline constexpr auto transpose    = "transpose";
    inline constexpr auto octave       = "octave";
    inline constexpr auto lockKey      = "lockKey";
    inline constexpr auto keepRhythm   = "keepRhythm";
    inline constexpr auto mode         = "mode";
    inline constexpr auto listen       = "listen";
    inline constexpr auto sound        = "sound";
    inline constexpr auto volume       = "volume";
    inline constexpr auto follow       = "follow";
    inline constexpr auto audioPoly    = "audioPoly";
    inline constexpr auto audioSens    = "audioSens";
    inline constexpr auto quantize     = "quantize";
    inline constexpr auto projectTempo = "projectTempo";
}

class MimicProcessor : public juce::AudioProcessor,
                       public juce::ChangeBroadcaster,
                       private juce::AudioProcessorValueTreeState::Listener,
                       private juce::Timer
{
public:
    MimicProcessor();
    ~MimicProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                     { return true; }

    const juce::String getName() const override         { return "Monie MIDI Mimic"; }
    bool acceptsMidi() const override                   { return true; }
    bool producesMidi() const override                  { return true; }
    bool isMidiEffect() const override                  { return false; }
    double getTailLengthSeconds() const override        { return 0.0; }

    int getNumPrograms() override                       { return 1; }
    int getCurrentProgram() override                    { return 0; }
    void setCurrentProgram (int) override               {}
    const juce::String getProgramName (int) override    { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // Message-thread API used by the editor
    juce::AudioProcessorValueTreeState apvts;

    static bool isMidiFile (const juce::File& f);
    static bool isAudioFile (const juce::File& f);
    static juce::String loadableWildcard();

    void loadFile (const juce::File& file);
    bool isBusy() const noexcept                { return busy.load(); }
    float getProgress() const noexcept          { return progress.load(); }
    juce::String getStatus() const              { return status; }
    bool statusIsError() const noexcept         { return statusError; }

    void generate (bool newSeed);
    bool canGoBack() const noexcept             { return seedHistory.size() > 1; }
    void goBack();
    void useGeneratedAsSource();

    mm::Phrase getSource() const;
    mm::Phrase getGenerated() const;
    bool hasSource() const;
    bool hasGenerated() const;
    juce::String getSourceInfo() const;
    juce::String getGeneratedInfo() const;

    bool exportPhrase (bool generatedOne, const juce::File& file, juce::String& error);
    juce::File writeTempMidiForDrag();
    juce::String suggestedFileName (bool generatedOne) const;
    juce::File getLastExportFolder() const      { return lastExportFolder; }
    void setLastExportFolder (const juce::File& f) { lastExportFolder = f; }

    void setPreviewPlaying (bool shouldPlay);
    bool isPreviewPlaying() const noexcept      { return previewPlaying.load(); }
    bool isHostPlaying() const noexcept         { return hostPlaying.load(); }
    double getPlayheadBeat() const noexcept     { return playheadBeat.load(); }
    bool isListeningToSource() const;

private:
    struct PlayNote { double start, length; int pitch, channel; float velocity; };
    struct ActiveNote { int pitch, channel; double endBeat; };

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void parameterChanged (const juce::String& id, float newValue) override;
    void timerCallback() override;

    mm::GenParams currentParams() const;
    void setSource (mm::Phrase phrase, const juce::String& origin);
    void regenerateWithCurrentSeed();
    void rebuildPlayback();
    void setStatus (const juce::String& text, bool isError = false);

    void schedule (double b0, double b1, double samplesPerBeat, int numSamples, juce::MidiBuffer& out);
    void allNotesOff (juce::MidiBuffer& out, int samplePos);

    // ---- content (message thread) ----
    mutable juce::CriticalSection dataLock;
    mm::Phrase source, generated;
    juce::String sourceOrigin;
    mm::StyleModel model;
    int analysedMode = -1;
    juce::uint32 seed = 1;
    std::vector<juce::uint32> seedHistory;
    double lastSimilarity = 0.0;
    juce::File lastExportFolder;

    // ---- background loading ----
    class LoadJob;
    std::unique_ptr<LoadJob> loadJob;
    std::atomic<bool> busy { false };
    std::atomic<float> progress { 0.0f };
    juce::String status;
    bool statusError = false;

    // ---- playback (audio thread) ----
    juce::SpinLock playLock;
    std::vector<PlayNote> playNotes;
    double playLength = 16.0;
    bool playReset = true;

    std::array<ActiveNote, 512> active {};
    int numActive = 0;
    bool wasRunning = false;
    double lastEndBeat = 0.0, previewBeat = 0.0;
    std::atomic<bool> previewPlaying { false }, previewRestart { false }, hostPlaying { false };
    std::atomic<bool> drumSound { false };
    std::atomic<double> hostBpm { 0.0 }, playheadBeat { -1.0 };
    std::atomic<bool> regenPending { false }, reanalysePending { false }, playbackDirty { false };
    double currentSampleRate = 44100.0;
    float lastGain = 0.5f;

    juce::Synthesiser synth;

    std::atomic<float>* followParam = nullptr;
    std::atomic<float>* soundParam = nullptr;
    std::atomic<float>* volumeParam = nullptr;

    JUCE_DECLARE_WEAK_REFERENCEABLE (MimicProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MimicProcessor)
};
