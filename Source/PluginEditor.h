#pragma once

#include "PluginProcessor.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include "gui/NeonLookAndFeel.h"
#include "gui/WaveformDisplay.h"
#include "gui/DragHandle.h"
#include "gui/MelodyStrip.h"
#include "gui/AutotunePanel.h"

//==============================================================================
// Phase 2 editor (CLAUDE.md §6): text input, Generate (+progress), a neon
// waveform of the rendered clip, and a Play/Stop/Loop audition transport.
// Voice selector, autotune panel, MIDI melody strip and the drag handle fill in
// across phases 3–5.
//==============================================================================
class VocalizerAudioProcessorEditor : public juce::AudioProcessorEditor,
                                      public  juce::DragAndDropContainer,
                                      private juce::Timer
{
public:
    explicit VocalizerAudioProcessorEditor (VocalizerAudioProcessor&);
    ~VocalizerAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void styleButton (juce::TextButton&, juce::Colour accent);

    VocalizerAudioProcessor& processorRef;
    NeonLookAndFeel neonLnf;

    juce::TextEditor   textInput;
    juce::TextButton   generateButton { "GENERATE" };
    juce::TextButton   playButton     { "PLAY" };
    juce::TextButton   stopButton     { "STOP" };
    juce::TextButton   loopButton     { "LOOP" };
    juce::TextButton   armButton      { "ARM" };
    juce::TextButton   clearButton    { "CLEAR" };
    juce::TextButton   autotuneButton { "AUTOTUNE" };
    juce::TextButton   syncButton     { "SYNC" };
    juce::Label        statusLabel;
    WaveformDisplay    waveform;
    DragHandle         dragHandle;
    MelodyStrip        melodyStrip;
    AutotunePanel      autotunePanel;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autotuneAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> syncAttachment;

    int lastSeenGeneration = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalizerAudioProcessorEditor)
};
