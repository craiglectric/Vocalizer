#include "NeonLookAndFeel.h"

const juce::Colour NeonLookAndFeel::base    { 0xff0a0a12 };
const juce::Colour NeonLookAndFeel::panel   { 0xff12121e };
const juce::Colour NeonLookAndFeel::cyan    { 0xff00f0ff };
const juce::Colour NeonLookAndFeel::magenta { 0xffff2cc4 };
const juce::Colour NeonLookAndFeel::dim     { 0xff2a2a3a };

NeonLookAndFeel::NeonLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, base);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
}

juce::Font NeonLookAndFeel::monoFont (float height, bool bold)
{
    juce::Font f (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(),
                                     height, bold ? juce::Font::bold : juce::Font::plain));
    return f;
}

void NeonLookAndFeel::glowArc (juce::Graphics& g, const juce::Path& p, juce::Colour c,
                               float thickness, float glow)
{
    // Outer halo: a few widening, fading strokes.
    const int layers = 4;
    for (int i = layers; i >= 1; --i)
    {
        const float t = thickness + (float) i * 3.0f;
        const float a = glow * 0.10f * (float) i / (float) layers;
        g.setColour (c.withAlpha (a));
        g.strokePath (p, juce::PathStrokeType (t, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    }
    // Core stroke.
    g.setColour (c.withAlpha (juce::jlimit (0.0f, 1.0f, 0.5f + 0.5f * glow)));
    g.strokePath (p, juce::PathStrokeType (thickness, juce::PathStrokeType::curved,
                                           juce::PathStrokeType::rounded));
}

void NeonLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                        float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                                        juce::Slider&)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (10.0f);
    const auto centre = bounds.getCentre();
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const float track  = radius - 8.0f;
    const float angle  = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    // Saturation/brightness climb with the amount: cyan -> magenta.
    const juce::Colour arcColour = NeonLookAndFeel::cyan.interpolatedWith (NeonLookAndFeel::magenta, sliderPos);

    // Unlit background track.
    {
        juce::Path bg;
        bg.addCentredArc (centre.x, centre.y, track, track, 0.0f,
                          rotaryStartAngle, rotaryEndAngle, true);
        g.setColour (NeonLookAndFeel::dim);
        g.strokePath (bg, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));
    }

    // Lit value arc with glow (glow scales with amount).
    if (sliderPos > 0.001f)
    {
        juce::Path arc;
        arc.addCentredArc (centre.x, centre.y, track, track, 0.0f,
                           rotaryStartAngle, angle, true);
        glowArc (g, arc, arcColour, 5.0f, 0.3f + 0.7f * sliderPos);
    }

    // Inner hub.
    const float hubR = track * 0.62f;
    g.setColour (NeonLookAndFeel::panel);
    g.fillEllipse (juce::Rectangle<float> (hubR * 2, hubR * 2).withCentre (centre));
    g.setColour (arcColour.withAlpha (0.6f));
    g.drawEllipse (juce::Rectangle<float> (hubR * 2, hubR * 2).withCentre (centre), 1.5f);

    // Pointer.
    juce::Path pointer;
    const float pLen = hubR * 0.9f;
    pointer.startNewSubPath (centre.x, centre.y);
    pointer.lineTo (centre.x + pLen * std::cos (angle - juce::MathConstants<float>::halfPi),
                    centre.y + pLen * std::sin (angle - juce::MathConstants<float>::halfPi));
    glowArc (g, pointer, arcColour, 3.0f, 0.3f + 0.7f * sliderPos);
}
