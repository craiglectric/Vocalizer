#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "NeonLookAndFeel.h"
#include "../midi/MelodyRecorder.h"

//==============================================================================
// MelodyStrip — small neon readout of the captured MIDI melody contour
// (CLAUDE.md §6). Draws each note as a horizontal segment positioned by time
// (x) and pitch (y).
//==============================================================================
class MelodyStrip : public juce::Component
{
public:
    MelodyStrip() = default;

    void setMelody (const Melody& m);
    void paint (juce::Graphics&) override;

private:
    Melody melody;
    int    lowNote = 48;
    int    highNote = 72;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MelodyStrip)
};
