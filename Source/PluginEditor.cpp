#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
VocalizerAudioProcessorEditor::VocalizerAudioProcessorEditor (VocalizerAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p),
      autotunePanel (p.apvts, Params::voiceNames())
{
    setLookAndFeel (&neonLnf);

    // Text input.
    textInput.setMultiLine (true);
    textInput.setReturnKeyStartsNewLine (true);
    textInput.setTextToShowWhenEmpty ("type words to sing…", NeonLookAndFeel::dim.brighter (0.4f));
    textInput.setColour (juce::TextEditor::backgroundColourId, NeonLookAndFeel::panel);
    textInput.setColour (juce::TextEditor::textColourId, NeonLookAndFeel::cyan.brighter (0.2f));
    textInput.setColour (juce::TextEditor::outlineColourId, NeonLookAndFeel::dim);
    textInput.setColour (juce::TextEditor::focusedOutlineColourId, NeonLookAndFeel::cyan);
    textInput.setColour (juce::CaretComponent::caretColourId, NeonLookAndFeel::magenta);
    textInput.setFont (NeonLookAndFeel::monoFont (15.0f));
    textInput.setText (processorRef.getText(), false);
    addAndMakeVisible (textInput);

    styleButton (generateButton, NeonLookAndFeel::magenta);
    styleButton (playButton,     NeonLookAndFeel::cyan);
    styleButton (stopButton,     NeonLookAndFeel::cyan);
    styleButton (loopButton,     NeonLookAndFeel::cyan);
    loopButton.setClickingTogglesState (true);

    generateButton.onClick = [this]
    {
        processorRef.setText (textInput.getText());
        processorRef.generate();
    };
    playButton.onClick = [this] { processorRef.play(); };
    stopButton.onClick = [this] { processorRef.stop(); };
    loopButton.onClick = [this] { processorRef.setLooping (loopButton.getToggleState()); };

    addAndMakeVisible (generateButton);
    addAndMakeVisible (playButton);
    addAndMakeVisible (stopButton);
    addAndMakeVisible (loopButton);

    statusLabel.setFont (NeonLookAndFeel::monoFont (11.0f));
    statusLabel.setColour (juce::Label::textColourId, NeonLookAndFeel::cyan.withAlpha (0.7f));
    statusLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (statusLabel);

    addAndMakeVisible (waveform);

    // MIDI melody capture (CLAUDE.md §4.3).
    styleButton (armButton,      NeonLookAndFeel::magenta);
    styleButton (clearButton,    NeonLookAndFeel::cyan);
    styleButton (autotuneButton, NeonLookAndFeel::magenta);
    styleButton (syncButton,     NeonLookAndFeel::cyan);
    armButton.setClickingTogglesState (true);
    autotuneButton.setClickingTogglesState (true);
    syncButton.setClickingTogglesState (true);
    syncButton.setTooltip ("Stretch the render to the captured MIDI clip length so it lines up with the project tempo.");

    armButton.onClick   = [this] { processorRef.setMelodyArmed (armButton.getToggleState()); };
    clearButton.onClick = [this]
    {
        processorRef.clearMelody();
        armButton.setToggleState (false, juce::dontSendNotification);
        melodyStrip.setMelody ({});
    };

    autotuneAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processorRef.apvts, ParamID::autotuneOn, autotuneButton);
    syncAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processorRef.apvts, ParamID::clipSync, syncButton);

    addAndMakeVisible (armButton);
    addAndMakeVisible (clearButton);
    addAndMakeVisible (autotuneButton);
    addAndMakeVisible (syncButton);
    addAndMakeVisible (melodyStrip);

    // Full voice + autotune panel (CLAUDE.md §6).
    autotunePanel.setVoiceIndex (
        (int) std::lround (processorRef.apvts.getRawParameterValue (ParamID::voicePreset)->load()));
    autotunePanel.onVoiceSelected = [this] (int idx)
    {
        if (auto* vp = processorRef.apvts.getParameter (ParamID::voicePreset))
            vp->setValueNotifyingHost (vp->convertTo0to1 ((float) idx));
        processorRef.applyVoiceDefaults (idx);
    };
    addAndMakeVisible (autotunePanel);

    dragHandle.getFileToDrag = [this] { return processorRef.getRenderedWavFile(); };
    dragHandle.isReady       = [this]
    {
        return processorRef.hasRenderedAudio()
            && processorRef.getRenderedWavFile().existsAsFile();
    };
    addAndMakeVisible (dragHandle);

    setSize (600, 820);
    startTimerHz (30);
}

VocalizerAudioProcessorEditor::~VocalizerAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void VocalizerAudioProcessorEditor::styleButton (juce::TextButton& b, juce::Colour accent)
{
    b.setColour (juce::TextButton::buttonColourId, NeonLookAndFeel::panel);
    b.setColour (juce::TextButton::buttonOnColourId, accent.withAlpha (0.35f));
    b.setColour (juce::TextButton::textColourOffId, accent);
    b.setColour (juce::TextButton::textColourOnId, accent.brighter (0.4f));
}

//==============================================================================
void VocalizerAudioProcessorEditor::timerCallback()
{
    processorRef.serviceMessageThread();

    const bool rendering = processorRef.isRendering();
    generateButton.setEnabled (! rendering);

    // Status / progress.
    if (rendering)
        statusLabel.setText ("RENDERING  " + juce::String (juce::roundToInt (processorRef.getProgress() * 100.0f)) + "%",
                             juce::dontSendNotification);
    else
        statusLabel.setText (processorRef.getStatusMessage(), juce::dontSendNotification);

    // New render? Refresh the waveform thumbnail.
    const int gen = processorRef.getRenderGeneration();
    if (gen != lastSeenGeneration)
    {
        lastSeenGeneration = gen;
        waveform.setThumbnail (processorRef.getThumbnail());
    }

    // Transport / playhead.
    const bool playing = processorRef.isPlaying();
    waveform.setPlaying (playing);
    if (playing)
        waveform.setPlayhead (processorRef.getPlayheadNormalized());

    playButton.setEnabled (processorRef.hasRenderedAudio());
    playButton.setButtonText (playing ? "PLAYING" : "PLAY");

    dragHandle.setReady (processorRef.hasRenderedAudio()
                         && processorRef.getRenderedWavFile().existsAsFile());

    // Melody capture readout.
    const bool armed = processorRef.isMelodyArmed();
    armButton.setButtonText (armed ? "ARM (REC)" : "ARM");
    melodyStrip.setMelody (processorRef.getMelodySnapshot());

    // Keep the voice combo in sync with the param (state recall / automation)
    // without re-triggering the preset-defaults push.
    const int vi = (int) std::lround (processorRef.apvts.getRawParameterValue (ParamID::voicePreset)->load());
    if (vi != autotunePanel.getVoiceIndex())
        autotunePanel.setVoiceIndex (vi);
}

//==============================================================================
void VocalizerAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (NeonLookAndFeel::base);

    auto top = getLocalBounds().removeFromTop (56);

    g.setColour (NeonLookAndFeel::cyan);
    g.setFont (NeonLookAndFeel::monoFont (26.0f, true));
    g.drawText ("VOCALIZER", top.reduced (22, 8), juce::Justification::centredLeft, false);

    g.setColour (NeonLookAndFeel::magenta.withAlpha (0.8f));
    g.setFont (NeonLookAndFeel::monoFont (11.0f));
    g.drawText ("COWDEN AUDIO  v" JucePlugin_VersionString,
                top.reduced (22, 8), juce::Justification::centredRight, false);
}

void VocalizerAudioProcessorEditor::resized()
{
    auto r = getLocalBounds().reduced (16);
    r.removeFromTop (44);   // title strip (minus the reduce)

    textInput.setBounds (r.removeFromTop (104));
    r.removeFromTop (8);

    // Melody capture row + contour.
    auto melodyButtons = r.removeFromTop (30);
    armButton.setBounds (melodyButtons.removeFromLeft (92));
    melodyButtons.removeFromLeft (8);
    clearButton.setBounds (melodyButtons.removeFromLeft (84));
    melodyButtons.removeFromLeft (8);
    autotuneButton.setBounds (melodyButtons.removeFromLeft (110));
    melodyButtons.removeFromLeft (8);
    syncButton.setBounds (melodyButtons.removeFromLeft (84));
    r.removeFromTop (6);
    melodyStrip.setBounds (r.removeFromTop (52));
    r.removeFromTop (10);

    // Voice + autotune panel.
    autotunePanel.setBounds (r.removeFromTop (150));
    r.removeFromTop (10);

    auto genRow = r.removeFromTop (40);
    generateButton.setBounds (genRow.removeFromLeft (200));
    genRow.removeFromLeft (12);
    statusLabel.setBounds (genRow);
    r.removeFromTop (10);

    waveform.setBounds (r.removeFromTop (130));
    r.removeFromTop (10);

    auto transport = r.removeFromTop (38);
    playButton.setBounds (transport.removeFromLeft (120));
    transport.removeFromLeft (8);
    stopButton.setBounds (transport.removeFromLeft (120));
    transport.removeFromLeft (8);
    loopButton.setBounds (transport.removeFromLeft (90));

    r.removeFromTop (12);
    dragHandle.setBounds (r.removeFromTop (46));
}
