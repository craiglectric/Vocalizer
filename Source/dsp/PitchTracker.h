#pragma once

#include <vector>
#include <cstdint>

//==============================================================================
// PitchTracker — monophonic f0 estimation via YIN (CLAUDE.md §4.4). Offline,
// frame-by-frame over a mono buffer; produces an f0 contour the PitchCorrector
// uses as the analysis pitch. Worker-thread only.
//==============================================================================
struct PitchContour
{
    std::vector<float> f0;     // Hz per frame (0 = unvoiced)
    int    hop = 256;
    int    firstCenter = 0;    // sample index of the first frame's centre
    double sampleRate = 48000.0;

    // Interpolated f0 (Hz) at an absolute sample index; 0 if unvoiced there.
    float  f0AtSample (int sampleIndex) const;
    bool   voicedAtSample (int sampleIndex) const;
};

class PitchTracker
{
public:
    static PitchContour analyze (const float* samples, int numSamples, double sampleRate,
                                 float fMin = 65.0f, float fMax = 500.0f);
};
