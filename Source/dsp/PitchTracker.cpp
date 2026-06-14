#include "PitchTracker.h"
#include <cmath>
#include <algorithm>

//==============================================================================
float PitchContour::f0AtSample (int sampleIndex) const
{
    if (f0.empty())
        return 0.0f;

    const float fpos = (float) (sampleIndex - firstCenter) / (float) hop;
    const int   i0   = (int) std::floor (fpos);
    const int   i1   = i0 + 1;
    const float frac = fpos - (float) i0;

    auto at = [this] (int i) -> float
    {
        if (i < 0) i = 0;
        if (i >= (int) f0.size()) i = (int) f0.size() - 1;
        return f0[(size_t) i];
    };

    const float a = at (i0);
    const float b = at (i1);

    // Only interpolate when both neighbours are voiced; otherwise take the
    // voiced one (avoids smearing pitch across a voicing boundary).
    if (a > 0.0f && b > 0.0f) return a + frac * (b - a);
    if (a > 0.0f)             return a;
    return b;
}

bool PitchContour::voicedAtSample (int sampleIndex) const
{
    if (f0.empty())
        return false;

    int i = (int) std::lround ((double) (sampleIndex - firstCenter) / (double) hop);
    i = std::clamp (i, 0, (int) f0.size() - 1);
    return f0[(size_t) i] > 0.0f;
}

//==============================================================================
PitchContour PitchTracker::analyze (const float* x, int N, double sr,
                                    float fMin, float fMax)
{
    PitchContour out;
    out.sampleRate = sr;

    const int tauMax = std::min (N - 1, (int) std::ceil (sr / (double) fMin));
    const int tauMin = std::max (2,     (int) std::floor (sr / (double) fMax));
    const int W      = std::max (1024, tauMax);          // window covers >1 period
    const int hop    = std::max (128, (int) (sr / 200.0)); // ~5 ms grid
    out.hop = hop;
    out.firstCenter = W / 2;

    if (N < W + tauMax)
        return out;   // too short to analyze

    const float threshold = 0.15f;   // YIN absolute threshold
    std::vector<float> diff   ((size_t) tauMax + 1, 0.0f);
    std::vector<float> cmnd   ((size_t) tauMax + 1, 0.0f);

    for (int start = 0; start + W + tauMax < N; start += hop)
    {
        // Difference function.
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            float sum = 0.0f;
            const float* a = x + start;
            const float* b = x + start + tau;
            for (int j = 0; j < W; ++j)
            {
                const float d = a[j] - b[j];
                sum += d * d;
            }
            diff[(size_t) tau] = sum;
        }

        // Cumulative mean normalized difference.
        cmnd[0] = 1.0f;
        float running = 0.0f;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            running += diff[(size_t) tau];
            cmnd[(size_t) tau] = running > 0.0f
                ? diff[(size_t) tau] * (float) tau / running
                : 1.0f;
        }

        // Absolute threshold: first local min below threshold, else global min.
        int tauEst = -1;
        for (int tau = tauMin; tau <= tauMax; ++tau)
        {
            if (cmnd[(size_t) tau] < threshold)
            {
                while (tau + 1 <= tauMax && cmnd[(size_t) (tau + 1)] < cmnd[(size_t) tau])
                    ++tau;
                tauEst = tau;
                break;
            }
        }

        float f0 = 0.0f;
        if (tauEst > 0)
        {
            // Parabolic interpolation around the dip for sub-sample tau.
            const int t = tauEst;
            float betterTau = (float) t;
            if (t > tauMin && t < tauMax)
            {
                const float s0 = cmnd[(size_t) (t - 1)];
                const float s1 = cmnd[(size_t) t];
                const float s2 = cmnd[(size_t) (t + 1)];
                const float denom = (2.0f * (2.0f * s1 - s2 - s0));
                if (std::abs (denom) > 1.0e-9f)
                    betterTau = (float) t + (s2 - s0) / denom;
            }
            if (betterTau > 0.0f)
            {
                const float cand = (float) sr / betterTau;
                if (cand >= fMin && cand <= fMax)
                    f0 = cand;
            }
        }

        out.f0.push_back (f0);
    }

    return out;
}
