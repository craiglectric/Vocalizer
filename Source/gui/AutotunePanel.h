#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "NeonLookAndFeel.h"
#include <functional>

//==============================================================================
// AutotunePanel — the voice selector + full autotune controls (CLAUDE.md §6).
// Combos for Voice / Mode / Key / Scale and neon knobs for Retune Speed,
// Strength, Formant, Rate and Output Gain, all attached to the APVTS.
//
// The Voice combo is managed manually (not via an attachment) so its onChange
// fires only on user selection — that's when the preset's default rate/autotune
// are applied. State recall / automation update it silently (no defaults push).
//==============================================================================
class AutotunePanel : public juce::Component
{
public:
    AutotunePanel (juce::AudioProcessorValueTreeState& apvts, const juce::StringArray& voiceNames);

    void paint (juce::Graphics&) override;
    void resized() override;

    // User picked a voice (index into the voice list).
    std::function<void (int)> onVoiceSelected;

    void setVoiceIndex (int index);            // silent (no onChange)
    int  getVoiceIndex() const { return voiceBox.getSelectedItemIndex(); }

private:
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Knob
    {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<SA> attachment;
    };

    void setupCombo (juce::ComboBox&, juce::Label&, const juce::String& title,
                     const juce::StringArray& items);
    void setupKnob  (Knob&, const juce::String& paramId, const juce::String& title);

    juce::AudioProcessorValueTreeState& state;

    juce::ComboBox voiceBox, modeBox, keyBox, scaleBox;
    juce::Label    voiceLbl, modeLbl, keyLbl, scaleLbl;
    std::unique_ptr<CA> modeAtt, keyAtt, scaleAtt;

    Knob speed, strength, formant, rate, gain;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutotunePanel)
};
