#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "MimicLookAndFeel.h"
#include "PianoRoll.h"

/** The "drag me into FL Studio" pad: dragging it hands the generated clip to the host as a .mid file. */
class DragPad : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit DragPad (MimicProcessor& p) : proc (p)
    {
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        setTooltip ("Drag this onto an FL Studio channel, the Playlist or a Piano roll to drop the generated MIDI in.");
    }

    void paint (juce::Graphics& g) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent&) override   { dragging = false; repaint(); }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }

private:
    MimicProcessor& proc;
    bool dragging = false;
};

/** A labelled rotary knob bound to a parameter. */
class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramID, const juce::String& name,
          juce::Colour colour, bool bipolar = false, const juce::String& tooltip = {});

    void resized() override;

    juce::Slider slider;
    juce::Label label;

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

class MimicEditor : public juce::AudioProcessorEditor,
                    public juce::FileDragAndDropTarget,
                    private juce::ChangeListener,
                    private juce::Timer
{
public:
    explicit MimicEditor (MimicProcessor&);
    ~MimicEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override  { setDropHighlight (true); }
    void fileDragExit (const juce::StringArray&) override             { setDropHighlight (false); }
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void refreshContent();
    void setDropHighlight (bool);
    void chooseFileToLoad();
    void exportClip (bool generatedOne);

    MimicProcessor& proc;
    MimicLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };

    juce::Label title, subtitle, status, sourceLabel, generatedLabel, sourceInfo, generatedInfo, genHeader, audioHeader;
    juce::TextButton loadButton { "Load MIDI / Audio" }, saveSourceButton { "Save as .mid" },
                     useAsSourceButton { "Use as source" }, generateButton { "GENERATE" }, backButton { "Back" },
                     playButton { "Play" }, exportButton { "Export .mid" };
    DragPad dragPad;
    PianoRoll sourceRoll, generatedRoll;

    Knob similarity, creativity, density, humanize, transpose, octave, sensitivity;
    juce::ComboBox barsBox, modeBox, listenBox;
    juce::Label barsLabel, modeLabel, polyLabel, volumeLabel;
    juce::ToggleButton lockKey { "Lock to key" }, keepRhythm { "Keep rhythm" }, sound { "Built-in sound" },
                       follow { "Follow FL transport" }, quantize { "Quantize" }, projectTempo { "Use project tempo" };
    juce::Slider poly, volume;

    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::vector<std::unique_ptr<ComboAttachment>> comboAttachments;
    std::vector<std::unique_ptr<ButtonAttachment>> buttonAttachments;
    std::vector<std::unique_ptr<SliderAttachment>> sliderAttachments;

    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> genPanel, audioPanel, bottomBar;
    bool dropHighlight = false, wasBusy = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MimicEditor)
};
