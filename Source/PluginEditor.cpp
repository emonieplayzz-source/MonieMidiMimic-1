#include "PluginEditor.h"

namespace
{
    constexpr int kWidth = 1000, kHeight = 680;

    void styleHeader (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (juce::FontOptions (12.5f, juce::Font::bold));
        l.setColour (juce::Label::textColourId, Palette::dim);
    }

    void styleInfo (juce::Label& l)
    {
        l.setFont (juce::FontOptions (13.0f));
        l.setColour (juce::Label::textColourId, Palette::text.withAlpha (0.85f));
        l.setMinimumHorizontalScale (0.7f);
    }

    juce::String signedInt (double v) { const int i = juce::roundToInt (v); return (i > 0 ? "+" : "") + juce::String (i); }
}

//==============================================================================
void DragPad::paint (juce::Graphics& g)
{
    using namespace juce;
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool enabled = proc.hasGenerated();
    const bool hot = enabled && (isMouseOver() || dragging);

    g.setColour ((hot ? Palette::teal.withAlpha (0.18f) : Palette::panelHi));
    g.fillRoundedRectangle (r, 8.0f);

    Path outline, border;
    outline.addRoundedRectangle (r.reduced (1.0f), 8.0f);
    const float dashes[] = { 5.0f, 4.0f };
    PathStrokeType (1.5f).createDashedStroke (border, outline, dashes, 2);
    g.setColour (enabled ? Palette::teal : Palette::dim.withAlpha (0.5f));
    g.fillPath (border);

    // little down arrow
    const float cx = r.getX() + 22.0f, cy = r.getCentreY();
    Path arrow;
    arrow.startNewSubPath (cx, cy - 8.0f);
    arrow.lineTo (cx, cy + 6.0f);
    arrow.startNewSubPath (cx - 6.0f, cy);
    arrow.lineTo (cx, cy + 7.0f);
    arrow.lineTo (cx + 6.0f, cy);
    g.strokePath (arrow, PathStrokeType (2.0f, PathStrokeType::curved, PathStrokeType::rounded));

    g.setColour (enabled ? Palette::text : Palette::dim);
    g.setFont (FontOptions (13.5f, Font::bold));
    g.drawFittedText ("DRAG MIDI INTO FL", getLocalBounds().withTrimmedLeft (40).withTrimmedRight (6), Justification::centredLeft, 1);
}

void DragPad::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || e.getDistanceFromDragStart() < 6 || ! proc.hasGenerated())
        return;

    dragging = true;
    repaint();

    const auto file = proc.writeTempMidiForDrag();
    if (file.existsAsFile())
    {
        juce::Component::SafePointer<DragPad> safe (this);
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this, [safe]
        {
            if (safe != nullptr) { safe->dragging = false; safe->repaint(); }
        });
    }
    else
    {
        dragging = false;
    }
}

//==============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramID, const juce::String& name,
            juce::Colour colour, bool bipolar, const juce::String& tooltip)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 16);
    slider.setColour (juce::Slider::rotarySliderFillColourId, colour);
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxTextColourId, Palette::text);
    slider.getProperties().set ("bipolar", bipolar);
    slider.setTooltip (tooltip);
    addAndMakeVisible (slider);

    label.setText (name, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (juce::FontOptions (12.5f));
    label.setColour (juce::Label::textColourId, Palette::dim);
    label.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (label);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramID, slider);
    slider.setDoubleClickReturnValue (true, state.getParameter (paramID)->convertFrom0to1 (state.getParameter (paramID)->getDefaultValue()));
}

void Knob::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromTop (16));
    slider.setBounds (r);
}

//==============================================================================
MimicEditor::MimicEditor (MimicProcessor& p)
    : AudioProcessorEditor (&p), proc (p), dragPad (p),
      sourceRoll (Palette::amber, "Drop a MIDI file, or a WAV / MP3 / FLAC loop, anywhere on this window.\nOr click Load MIDI / Audio."),
      generatedRoll (Palette::pink, "Your new MIDI will show up here."),
      similarity  (p.apvts, ParamIDs::similarity, "Similarity", Palette::pink, false,
                   "How close the result stays to your source. 100% = nearly the same, 0% = loosely inspired."),
      creativity  (p.apvts, ParamIDs::creativity, "Creativity", Palette::pink, false,
                   "How adventurous the new notes and rhythms are."),
      density     (p.apvts, ParamIDs::density, "Density", Palette::teal, true,
                   "Fewer notes (left) or busier (right)."),
      humanize    (p.apvts, ParamIDs::humanize, "Humanize", Palette::teal, false,
                   "Loosens timing and velocity so it feels played rather than programmed."),
      transpose   (p.apvts, ParamIDs::transpose, "Transpose", Palette::amber, true, "Shift the result up or down in semitones (ignored for drums)."),
      octave      (p.apvts, ParamIDs::octave, "Octave", Palette::amber, true, "Shift the result by octaves (ignored for drums)."),
      sensitivity (p.apvts, ParamIDs::audioSens, "Sensitivity", Palette::teal, false,
                   "Higher catches quieter notes and hits when converting audio (and more mistakes).")
{
    setLookAndFeel (&lnf);

    title.setText ("MONIE MIDI MIMIC", juce::dontSendNotification);
    title.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, Palette::text);
    subtitle.setText ("load a MIDI or a loop - get new MIDI that sounds like it", juce::dontSendNotification);
    subtitle.setFont (juce::FontOptions (12.5f));
    subtitle.setColour (juce::Label::textColourId, Palette::dim);
    status.setFont (juce::FontOptions (13.0f));
    status.setJustificationType (juce::Justification::centredRight);
    status.setMinimumHorizontalScale (0.75f);

    styleHeader (sourceLabel, "SOURCE");
    styleHeader (generatedLabel, "GENERATED");
    styleHeader (genHeader, "GENERATOR");
    styleHeader (audioHeader, juce::String::fromUTF8 ("AUDIO \xe2\x86\x92 MIDI"));
    styleInfo (sourceInfo);
    styleInfo (generatedInfo);

    for (auto* c : std::initializer_list<juce::Component*> {
             &title, &subtitle, &status, &sourceLabel, &generatedLabel, &sourceInfo, &generatedInfo, &genHeader, &audioHeader,
             &loadButton, &saveSourceButton, &useAsSourceButton, &generateButton, &backButton, &playButton, &exportButton,
             &dragPad, &sourceRoll, &generatedRoll,
             &similarity, &creativity, &density, &humanize, &transpose, &octave, &sensitivity,
             &barsBox, &modeBox, &listenBox, &barsLabel, &modeLabel, &polyLabel, &volumeLabel,
             &lockKey, &keepRhythm, &sound, &follow, &quantize, &projectTempo, &poly, &volume })
        addAndMakeVisible (c);

    // Value text for knobs
    for (auto* k : { &similarity, &creativity, &humanize, &sensitivity })
        k->slider.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v)) + "%"; };
    density.slider.textFromValueFunction   = [] (double v) { return signedInt (v) + "%"; };
    transpose.slider.textFromValueFunction = [] (double v) { return signedInt (v) + " st"; };
    octave.slider.textFromValueFunction    = [] (double v) { return signedInt (v) + " oct"; };
    for (auto* k : { &similarity, &creativity, &density, &humanize, &transpose, &octave, &sensitivity })
        k->slider.updateText();

    // Combos
    auto setupCombo = [this] (juce::ComboBox& box, juce::Label& lbl, const char* id, const juce::String& text)
    {
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (id)))
            box.addItemList (choice->choices, 1);
        comboAttachments.push_back (std::make_unique<ComboAttachment> (proc.apvts, id, box));
        lbl.setText (text, juce::dontSendNotification);
        lbl.setFont (juce::FontOptions (13.0f));
        lbl.setColour (juce::Label::textColourId, Palette::dim);
    };
    setupCombo (barsBox, barsLabel, ParamIDs::bars, "Length");
    setupCombo (modeBox, modeLabel, ParamIDs::mode, "Mode");
    juce::Label unused;
    setupCombo (listenBox, unused, ParamIDs::listen, {});
    barsBox.setTooltip ("How long the new clip is.");
    modeBox.setTooltip ("Auto picks drums for channel-10 MIDI (or audio files named drum/kick/hat...). Force it here.");
    listenBox.setTooltip ("Hear the generated clip or the original source.");

    // Toggles
    auto attachToggle = [this] (juce::ToggleButton& b, const char* id, const juce::String& tip)
    {
        buttonAttachments.push_back (std::make_unique<ButtonAttachment> (proc.apvts, id, b));
        b.setTooltip (tip);
    };
    attachToggle (lockKey, ParamIDs::lockKey, "Keep every new note in the key detected from your source.");
    attachToggle (keepRhythm, ParamIDs::keepRhythm, "Keep the source rhythm exactly and only change notes. (Drums: keeps the kick pattern.)");
    attachToggle (sound, ParamIDs::sound, "Play through the plugin's own simple sound. Turn off when sending the MIDI to another instrument.");
    attachToggle (follow, ParamIDs::follow, "Play in sync with FL Studio when you press play in FL.");
    attachToggle (quantize, ParamIDs::quantize, "Snap converted audio notes to a 1/16 grid.");
    attachToggle (projectTempo, ParamIDs::projectTempo, "Use FL's tempo when converting audio, instead of detecting it.");

    // Linear sliders
    poly.setSliderStyle (juce::Slider::LinearHorizontal);
    poly.setTextBoxStyle (juce::Slider::TextBoxRight, false, 30, 18);
    poly.setTooltip ("How many notes can sound at once when converting audio. 1 = melody / bass line, 3-4 = chords.");
    sliderAttachments.push_back (std::make_unique<SliderAttachment> (proc.apvts, ParamIDs::audioPoly, poly));
    polyLabel.setText ("Notes at once", juce::dontSendNotification);
    polyLabel.setFont (juce::FontOptions (13.0f));
    polyLabel.setColour (juce::Label::textColourId, Palette::dim);

    volume.setSliderStyle (juce::Slider::LinearHorizontal);
    volume.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    volume.setTooltip ("Volume of the built-in sound.");
    sliderAttachments.push_back (std::make_unique<SliderAttachment> (proc.apvts, ParamIDs::volume, volume));
    volumeLabel.setText ("Vol", juce::dontSendNotification);
    volumeLabel.setFont (juce::FontOptions (13.0f));
    volumeLabel.setColour (juce::Label::textColourId, Palette::dim);

    // Buttons
    generateButton.getProperties().set ("primary", true);
    generateButton.setTooltip ("Make a new take. Each click gives a different result in the same style.");
    backButton.setTooltip ("Go back to the previous take.");
    loadButton.setTooltip ("Load a .mid file, or a WAV / MP3 / FLAC / AIFF / OGG to convert to MIDI first.");
    saveSourceButton.setTooltip ("Save the source as a .mid (handy after converting audio).");
    useAsSourceButton.setTooltip ("Make this take the new source, to keep evolving it.");
    exportButton.setTooltip ("Save the generated MIDI as a .mid file.");
    playButton.setTooltip ("Preview without starting FL's transport.");

    loadButton.onClick        = [this] { chooseFileToLoad(); };
    generateButton.onClick    = [this] { proc.generate (true); };
    backButton.onClick        = [this] { proc.goBack(); };
    useAsSourceButton.onClick = [this] { proc.useGeneratedAsSource(); };
    saveSourceButton.onClick  = [this] { exportClip (false); };
    exportButton.onClick      = [this] { exportClip (true); };
    playButton.onClick        = [this] { proc.setPreviewPlaying (! proc.isPreviewPlaying()); };

    proc.addChangeListener (this);
    refreshContent();
    setSize (kWidth, kHeight);
    startTimerHz (30);
}

MimicEditor::~MimicEditor()
{
    stopTimer();
    proc.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

//==============================================================================
void MimicEditor::paint (juce::Graphics& g)
{
    g.fillAll (Palette::bg);

    auto panel = [&g] (juce::Rectangle<int> r)
    {
        g.setColour (Palette::panel);
        g.fillRoundedRectangle (r.toFloat(), 10.0f);
        g.setColour (Palette::line);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 10.0f, 1.0f);
    };
    panel (genPanel);
    panel (audioPanel);
    panel (bottomBar);

    // accent strip under the title
    g.setGradientFill (juce::ColourGradient (Palette::pink, 20.0f, 0.0f, Palette::teal, 260.0f, 0.0f, false));
    g.fillRoundedRectangle (20.0f, 54.0f, 120.0f, 2.5f, 1.25f);

    if (proc.isBusy())
    {
        auto bar = juce::Rectangle<float> ((float) status.getX(), (float) status.getBottom() + 2.0f, (float) status.getWidth(), 3.0f);
        g.setColour (Palette::line);
        g.fillRoundedRectangle (bar, 1.5f);
        g.setColour (Palette::teal);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.02f, 1.0f, proc.getProgress())), 1.5f);
    }
}

void MimicEditor::resized()
{
    using R = juce::Rectangle<int>;
    const int margin = 16, rightW = 280, gap = 12;
    const int leftW = getWidth() - margin * 2 - rightW - gap;
    const int rightX = margin + leftW + gap;

    // Header
    title.setBounds (16, 8, 300, 30);
    subtitle.setBounds (18, 34, 360, 18);
    loadButton.setBounds (getWidth() - margin - 180, 14, 180, 34);
    status.setBounds (390, 12, loadButton.getX() - 400, 36);

    // Source
    sourceLabel.setBounds (margin, 66, 70, 20);
    saveSourceButton.setBounds (margin + leftW - 104, 64, 104, 24);
    sourceInfo.setBounds (margin + 66, 66, saveSourceButton.getX() - margin - 72, 20);
    sourceRoll.setBounds (margin, 92, leftW, 172);

    // Generated
    generatedLabel.setBounds (margin, 276, 90, 20);
    useAsSourceButton.setBounds (margin + leftW - 112, 274, 112, 24);
    generatedInfo.setBounds (margin + 88, 276, useAsSourceButton.getX() - margin - 94, 20);
    generatedRoll.setBounds (margin, 302, leftW, 286);

    // Generator panel
    genPanel = R (rightX, 64, rightW, 362);
    auto gp = genPanel.reduced (10, 8);
    genHeader.setBounds (gp.removeFromTop (20));
    const int cellW = gp.getWidth() / 3;
    auto row1 = gp.removeFromTop (90);
    for (auto* k : { &similarity, &creativity, &density }) k->setBounds (row1.removeFromLeft (cellW));
    auto row2 = gp.removeFromTop (90);
    for (auto* k : { &humanize, &transpose, &octave }) k->setBounds (row2.removeFromLeft (cellW));
    gp.removeFromTop (6);
    auto barsRow = gp.removeFromTop (26);
    barsLabel.setBounds (barsRow.removeFromLeft (60));
    barsBox.setBounds (barsRow);
    gp.removeFromTop (6);
    auto modeRow = gp.removeFromTop (26);
    modeLabel.setBounds (modeRow.removeFromLeft (60));
    modeBox.setBounds (modeRow);
    gp.removeFromTop (6);
    auto toggleRow = gp.removeFromTop (24);
    lockKey.setBounds (toggleRow.removeFromLeft (toggleRow.getWidth() / 2));
    keepRhythm.setBounds (toggleRow);
    gp.removeFromTop (8);
    auto genRow = gp.removeFromTop (44);
    backButton.setBounds (genRow.removeFromRight (64));
    genRow.removeFromRight (8);
    generateButton.setBounds (genRow);

    // Audio panel
    audioPanel = R (rightX, 434, rightW, 154);
    auto ap = audioPanel.reduced (10, 8);
    audioHeader.setBounds (ap.removeFromTop (20));
    auto polyRow = ap.removeFromTop (26);
    polyLabel.setBounds (polyRow.removeFromLeft (100));
    poly.setBounds (polyRow);
    ap.removeFromTop (4);
    sensitivity.setBounds (ap.removeFromLeft (90));
    ap.removeFromLeft (8);
    ap.removeFromTop (10);
    quantize.setBounds (ap.removeFromTop (28));
    projectTempo.setBounds (ap.removeFromTop (28));

    // Bottom bar
    bottomBar = R (margin, 600, getWidth() - margin * 2, 64);
    auto bb = bottomBar.reduced (10, 11);
    playButton.setBounds (bb.removeFromLeft (84));
    bb.removeFromLeft (8);
    listenBox.setBounds (bb.removeFromLeft (118).withSizeKeepingCentre (118, 30));
    bb.removeFromLeft (12);
    dragPad.setBounds (bb.removeFromRight (196));
    bb.removeFromRight (8);
    exportButton.setBounds (bb.removeFromRight (110));
    bb.removeFromRight (12);
    sound.setBounds (bb.removeFromLeft (130));
    volumeLabel.setBounds (bb.removeFromLeft (28));
    volume.setBounds (bb.removeFromLeft (86));
    bb.removeFromLeft (10);
    follow.setBounds (bb);
}

//==============================================================================
bool MimicEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
    {
        const juce::File file (f);
        if (MimicProcessor::isMidiFile (file) || MimicProcessor::isAudioFile (file))
            return true;
    }
    return false;
}

void MimicEditor::filesDropped (const juce::StringArray& files, int, int)
{
    setDropHighlight (false);
    for (auto& f : files)
    {
        const juce::File file (f);
        if (MimicProcessor::isMidiFile (file) || MimicProcessor::isAudioFile (file))
        {
            proc.loadFile (file);
            return;
        }
    }
}

void MimicEditor::setDropHighlight (bool h)
{
    dropHighlight = h;
    sourceRoll.setHighlight (h);
}

void MimicEditor::chooseFileToLoad()
{
    chooser = std::make_unique<juce::FileChooser> ("Load a MIDI or audio file",
                                                   juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                   MimicProcessor::loadableWildcard());
    juce::Component::SafePointer<MimicEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe] (const juce::FileChooser& fc)
    {
        if (safe == nullptr) return;
        const auto f = fc.getResult();
        if (f != juce::File())
            safe->proc.loadFile (f);
    });
}

void MimicEditor::exportClip (bool generatedOne)
{
    if (generatedOne ? ! proc.hasGenerated() : ! proc.hasSource())
        return;

    auto folder = proc.getLastExportFolder();
    folder.createDirectory();

    chooser = std::make_unique<juce::FileChooser> (generatedOne ? "Export generated MIDI" : "Save source as MIDI",
                                                   folder.getChildFile (proc.suggestedFileName (generatedOne)), "*.mid");
    juce::Component::SafePointer<MimicEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [safe, generatedOne] (const juce::FileChooser& fc)
    {
        if (safe == nullptr) return;
        const auto f = fc.getResult();
        if (f == juce::File()) return;

        juce::String err;
        if (! safe->proc.exportPhrase (generatedOne, f, err))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Couldn't save", err);
    });
}

//==============================================================================
void MimicEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshContent();
}

void MimicEditor::refreshContent()
{
    sourceRoll.setPhrase (proc.getSource());
    generatedRoll.setPhrase (proc.getGenerated());
    sourceInfo.setText (proc.getSourceInfo(), juce::dontSendNotification);
    generatedInfo.setText (proc.getGeneratedInfo(), juce::dontSendNotification);

    const bool hasSrc = proc.hasSource(), hasGen = proc.hasGenerated();
    generateButton.setEnabled (hasSrc && ! proc.isBusy());
    backButton.setEnabled (proc.canGoBack());
    saveSourceButton.setEnabled (hasSrc);
    useAsSourceButton.setEnabled (hasGen);
    exportButton.setEnabled (hasGen);
    playButton.setEnabled (hasSrc);
    loadButton.setEnabled (true);
    dragPad.repaint();
    timerCallback();
    repaint();
}

void MimicEditor::timerCallback()
{
    const double beat = proc.getPlayheadBeat();
    const bool src = proc.isListeningToSource();
    sourceRoll.setPlayhead (src ? beat : -1.0);
    generatedRoll.setPlayhead (src ? -1.0 : beat);

    auto text = proc.getStatus();
    if (proc.isBusy())
        text << "  " << juce::roundToInt (proc.getProgress() * 100.0f) << "%";
    if (status.getText() != text)
    {
        status.setText (text, juce::dontSendNotification);
        status.setColour (juce::Label::textColourId, proc.statusIsError() ? Palette::red : Palette::dim);
    }

    const bool previewing = proc.isPreviewPlaying();
    const auto playText = previewing ? juce::String ("Stop") : juce::String ("Play");
    if (playButton.getButtonText() != playText)
    {
        playButton.setButtonText (playText);
        playButton.setToggleState (previewing, juce::dontSendNotification);
    }

    if (proc.isBusy())
        repaint (status.getX(), status.getBottom(), status.getWidth(), 6);

    if (wasBusy != proc.isBusy())
    {
        wasBusy = proc.isBusy();
        generateButton.setEnabled (proc.hasSource() && ! wasBusy);
        repaint();
    }
}
