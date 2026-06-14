#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "NeonLookAndFeel.h"
#include <vector>

//==============================================================================
// WaveformDisplay — neon min/max waveform of the rendered clip with a playhead
// (CLAUDE.md §6). Phase 3 adds the drag-to-track affordance on top of this.
//
// Fed a precomputed min/max thumbnail (computed off this component when a render
// completes) so paint() stays cheap. All on the message thread.
//==============================================================================
class WaveformDisplay : public juce::Component
{
public:
    WaveformDisplay() = default;

    // thumbnail = interleaved {min, max} pairs, one per column. Empty = no clip.
    void setThumbnail (std::vector<float> minMaxPairs);
    void clearThumbnail();

    void setPlayhead (float normalised);   // 0..1, <0 hides it
    void setPlaying (bool shouldShow) { showPlayhead = shouldShow; repaint(); }

    void paint (juce::Graphics&) override;

    // Builds a {min,max}-per-column thumbnail from a mono buffer.
    static std::vector<float> buildThumbnail (const float* samples, int numSamples,
                                              int numColumns);

private:
    std::vector<float> thumb;     // {min,max} pairs
    float playhead = 0.0f;
    bool  showPlayhead = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformDisplay)
};
