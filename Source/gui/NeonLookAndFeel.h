#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

//==============================================================================
// NeonLookAndFeel — dark cyberpunk styling shared across the Cowden Audio
// "-izer" plugins (Computerizer / CrateID / Vocalizer). Near-black base, neon
// cyan + magenta accents, outer glow on active elements, monospaced display
// type. Drives rotary knobs' neon arcs (brightness/saturation rise with value).
//==============================================================================
class NeonLookAndFeel : public juce::LookAndFeel_V4
{
public:
    NeonLookAndFeel();

    // Palette (shared with the components).
    static const juce::Colour base;     // background
    static const juce::Colour panel;    // slightly lifted panel
    static const juce::Colour cyan;
    static const juce::Colour magenta;
    static const juce::Colour dim;       // unlit strokes/text

    static juce::Font monoFont (float height, bool bold = false);

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider&) override;

private:
    // Draws a glowing arc by layering translucent strokes outward.
    static void glowArc (juce::Graphics&, const juce::Path&, juce::Colour,
                         float thickness, float glow);
};
