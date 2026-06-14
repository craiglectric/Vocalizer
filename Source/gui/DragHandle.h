#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "NeonLookAndFeel.h"
#include <functional>

//==============================================================================
// DragHandle — the "drag to track" affordance (CLAUDE.md §4.6 / §6). A glowing
// neon chip the user grabs and drops onto a DAW audio track; it launches an
// external (OS-level) file drag of the rendered WAV, which Ableton imports as a
// clip. Lit when a clip exists, dimmed otherwise.
//==============================================================================
class DragHandle : public juce::Component
{
public:
    DragHandle();

    // Supplied by the editor: the WAV to drag, and whether a clip is ready.
    std::function<juce::File()> getFileToDrag;
    std::function<bool()>       isReady;

    // Called by the editor timer to refresh the lit/dimmed look.
    void setReady (bool ready);

    void paint (juce::Graphics&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit  (const juce::MouseEvent&) override;

private:
    bool ready    = false;
    bool dragging = false;
    bool hovered  = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DragHandle)
};
