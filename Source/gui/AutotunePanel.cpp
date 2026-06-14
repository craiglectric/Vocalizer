#include "AutotunePanel.h"
#include "../params/Parameters.h"

AutotunePanel::AutotunePanel (juce::AudioProcessorValueTreeState& apvts,
                              const juce::StringArray& voiceNames)
    : state (apvts)
{
    // Voice combo — managed manually (see header note).
    setupCombo (voiceBox, voiceLbl, "VOICE", voiceNames);
    voiceBox.onChange = [this]
    {
        if (onVoiceSelected != nullptr)
            onVoiceSelected (voiceBox.getSelectedItemIndex());
    };

    setupCombo (modeBox,  modeLbl,  "MODE",  Params::autotuneModeNames());
    setupCombo (keyBox,   keyLbl,   "KEY",   Params::keyNames());
    setupCombo (scaleBox, scaleLbl, "SCALE", Params::scaleNames());

    modeAtt  = std::make_unique<CA> (state, ParamID::autotuneMode, modeBox);
    keyAtt   = std::make_unique<CA> (state, ParamID::key,          keyBox);
    scaleAtt = std::make_unique<CA> (state, ParamID::scale,        scaleBox);

    setupKnob (speed,    ParamID::retuneSpeed,     "SPEED");
    setupKnob (strength, ParamID::strength,        "STRENGTH");
    setupKnob (formant,  ParamID::formantPreserve, "FORMANT");
    setupKnob (rate,     ParamID::speakingRate,    "RATE");
    setupKnob (gain,     ParamID::outputGain,      "GAIN");
}

void AutotunePanel::setupCombo (juce::ComboBox& box, juce::Label& label,
                                const juce::String& title, const juce::StringArray& items)
{
    box.addItemList (items, 1);
    box.setColour (juce::ComboBox::backgroundColourId, NeonLookAndFeel::panel);
    box.setColour (juce::ComboBox::textColourId, NeonLookAndFeel::cyan);
    box.setColour (juce::ComboBox::outlineColourId, NeonLookAndFeel::dim);
    box.setColour (juce::ComboBox::arrowColourId, NeonLookAndFeel::magenta);
    addAndMakeVisible (box);

    label.setText (title, juce::dontSendNotification);
    label.setFont (NeonLookAndFeel::monoFont (10.0f, true));
    label.setColour (juce::Label::textColourId, NeonLookAndFeel::cyan.withAlpha (0.6f));
    label.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (label);
}

void AutotunePanel::setupKnob (Knob& k, const juce::String& paramId, const juce::String& title)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
    k.slider.setColour (juce::Slider::textBoxTextColourId, NeonLookAndFeel::cyan.withAlpha (0.8f));
    k.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (k.slider);
    k.attachment = std::make_unique<SA> (state, paramId, k.slider);

    k.label.setText (title, juce::dontSendNotification);
    k.label.setFont (NeonLookAndFeel::monoFont (10.0f, true));
    k.label.setColour (juce::Label::textColourId, NeonLookAndFeel::magenta.withAlpha (0.75f));
    k.label.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (k.label);
}

void AutotunePanel::setVoiceIndex (int index)
{
    voiceBox.setSelectedItemIndex (index, juce::dontSendNotification);
}

void AutotunePanel::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (NeonLookAndFeel::panel.darker (0.3f));
    g.fillRoundedRectangle (b, 8.0f);
    g.setColour (NeonLookAndFeel::dim);
    g.drawRoundedRectangle (b, 8.0f, 1.2f);
}

void AutotunePanel::resized()
{
    auto r = getLocalBounds().reduced (12);

    // Row 1: combos. Voice gets the most width.
    auto row = r.removeFromTop (46);
    auto comboCell = [&] (juce::ComboBox& box, juce::Label& lbl, int w)
    {
        auto cell = row.removeFromLeft (w);
        lbl.setBounds (cell.removeFromTop (14));
        box.setBounds (cell.reduced (0, 1));
        row.removeFromLeft (8);
    };
    const int total = row.getWidth();
    comboCell (voiceBox, voiceLbl, juce::roundToInt (total * 0.34f));
    comboCell (modeBox,  modeLbl,  juce::roundToInt (total * 0.26f));
    comboCell (keyBox,   keyLbl,   juce::roundToInt (total * 0.13f));
    comboCell (scaleBox, scaleLbl, row.getWidth());

    r.removeFromTop (8);

    // Row 2: five knobs.
    auto knobs = r;
    Knob* all[] = { &speed, &strength, &formant, &rate, &gain };
    const int kw = knobs.getWidth() / 5;
    for (auto* k : all)
    {
        auto cell = knobs.removeFromLeft (kw);
        k->label.setBounds (cell.removeFromTop (14));
        k->slider.setBounds (cell);
    }
}
