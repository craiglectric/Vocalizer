#include "PitchCorrector.h"
#include "PitchTracker.h"
#include <cmath>
#include <algorithm>
#include <array>

//==============================================================================
float PitchCorrector::midiToHz (float midi)
{
    return 440.0f * std::pow (2.0f, (midi - 69.0f) / 12.0f);
}

float PitchCorrector::hzToMidi (float hz)
{
    return hz > 0.0f ? 69.0f + 12.0f * std::log2 (hz / 440.0f) : 0.0f;
}

namespace
{
    // Scale degree tables, indexed to match Params::scaleNames() in Parameters.cpp:
    // CHROMATIC, MAJOR, MINOR, DORIAN, MIXOLYDIAN, PENTATONIC MAJ, PENTATONIC MIN.
    const std::vector<std::vector<int>>& scaleTable()
    {
        static const std::vector<std::vector<int>> tables
        {
            { 0,1,2,3,4,5,6,7,8,9,10,11 },   // Chromatic
            { 0,2,4,5,7,9,11 },              // Major
            { 0,2,3,5,7,8,10 },              // Minor (natural)
            { 0,2,3,5,7,9,10 },              // Dorian
            { 0,2,4,5,7,9,10 },              // Mixolydian
            { 0,2,4,7,9 },                   // Pentatonic major
            { 0,3,5,7,10 },                  // Pentatonic minor
        };
        return tables;
    }
}

float PitchCorrector::snapToScale (float midi, int key, int scale)
{
    const auto& tables = scaleTable();
    if (scale < 0 || scale >= (int) tables.size())
        return midi;

    const auto& degrees = tables[(size_t) scale];

    float best = midi;
    float bestDist = 1.0e9f;
    const int centre = (int) std::lround (midi);

    for (int oct = -1; oct <= 1; ++oct)
    {
        for (int deg : degrees)
        {
            // Candidate note in this octave band with the right pitch class.
            const int pc = ((key + deg) % 12 + 12) % 12;
            int candidate = (centre / 12 + oct) * 12 + pc;
            // also consider the neighbour octave alignment
            for (int k = -1; k <= 1; ++k)
            {
                const int c = candidate + k * 12;
                const float dist = std::abs ((float) c - midi);
                if (dist < bestDist) { bestDist = dist; best = (float) c; }
            }
        }
    }
    return best;
}

//==============================================================================
juce::AudioBuffer<float> PitchCorrector::process (const juce::AudioBuffer<float>& input,
                                                  double sampleRate,
                                                  const Melody& melody,
                                                  const Params& params)
{
    const int N = input.getNumSamples();
    if (N <= 0)
        return juce::AudioBuffer<float> (1, 0);

    const float* x = input.getReadPointer (0);
    const double sr = sampleRate;

    const bool useMelody = (params.mode == 0) && ! melody.empty() && melody.durationSec > 0.0;

    // 1) Output length. Clip-sync stretches the speech so the rendered clip spans
    //    the captured melody (= MIDI clip) length; otherwise it stays natural.
    int outN = N;
    if (params.clipSync && useMelody)
    {
        const double maxSecs = 60.0;   // guard against absurd stretch ratios
        outN = (int) std::clamp (std::llround (melody.durationSec * sr),
                                 (long long) (N / 8 + 1), (long long) (maxSecs * sr));
    }

    juce::AudioBuffer<float> output (1, outN);
    output.clear();

    const double timeScale = (double) N / (double) outN;   // input samples per output sample

    // 2) Analyze source pitch (over the input).
    const PitchContour contour = PitchTracker::analyze (x, N, sr);

    // 3) Per-OUTPUT-sample source MIDI (from the time-mapped input) + target MIDI.
    std::vector<float> srcMidi ((size_t) outN, 0.0f);
    std::vector<float> tgtMidi ((size_t) outN, 0.0f);
    std::vector<char>  voiced  ((size_t) outN, 0);

    float lastTarget = -1.0f;

    for (int n = 0; n < outN; ++n)
    {
        const int   inIdx = (int) ((double) n * timeScale);
        const float f0 = contour.f0AtSample (inIdx);
        const bool  v  = f0 > 0.0f;
        voiced[(size_t) n] = v ? 1 : 0;
        const float sMidi = v ? hzToMidi (f0) : 0.0f;
        srcMidi[(size_t) n] = sMidi;

        float target = -1.0f;
        if (useMelody)
        {
            // Output spans [0, melody.durationSec]; in speech-paced mode outN == N
            // so this still maps the melody across the speech. In clip-sync mode it
            // is the note active at the real time.
            const double mt = ((double) n / (double) outN) * melody.durationSec;
            const int    note = melody.noteAt (mt);
            if (note >= 0)      { target = (float) note; lastTarget = target; }
            else if (lastTarget >= 0.0f) target = lastTarget;     // hold last note over gaps
        }

        if (target < 0.0f)
            target = v ? snapToScale (sMidi, params.key, params.scale) : sMidi;

        tgtMidi[(size_t) n] = target;
    }

    // 4) Glide: one-pole smooth the target in the MIDI domain (0 ms = hard snap).
    if (params.retuneSpeedMs > 0.5f && outN > 0)
    {
        const float a = std::exp (-1.0f / ((params.retuneSpeedMs * 0.001f) * (float) sr));
        float s = tgtMidi[0];
        for (int n = 0; n < outN; ++n)
        {
            s = a * s + (1.0f - a) * tgtMidi[(size_t) n];
            tgtMidi[(size_t) n] = s;
        }
    }

    // 5) TD-PSOLA with optional time-stretch. Analysis marks are placed over the
    //    INPUT at source-period spacing; each output grain draws content from the
    //    mark nearest the time-mapped input position and is laid down at the
    //    output position spaced by the target period. Output spacing sets the
    //    pitch (target); the input/output time map sets the duration (clip). At
    //    formantPreserve = 1 grains are unresampled, so formants are preserved;
    //    lower values resample each grain so the formants follow the pitch.
    std::vector<float> acc  ((size_t) outN, 0.0f);
    std::vector<float> norm ((size_t) outN, 0.0f);

    const float strength = std::clamp (params.strength, 0.0f, 1.0f);
    // How far formants follow the pitch shift: 0 = preserved, 1 = fully moved.
    const float formantMove = 1.0f - std::clamp (params.formantPreserve, 0.0f, 1.0f);
    const int   minP = std::max (2, (int) (sr / 1000.0));  // 1000 Hz ceiling
    const int   maxP = (int) (sr / 60.0);                  // 60 Hz floor
    const int   defaultP = (int) (sr / 120.0);             // unvoiced grain spacing

    auto sourcePeriodAt = [&] (int inIdx) -> int
    {
        const float f0 = contour.f0AtSample (inIdx);
        if (f0 > 0.0f)
            return std::clamp ((int) std::lround (sr / f0), minP, maxP);
        return defaultP;
    };

    // Analysis marks over the input.
    std::vector<int> marks;
    for (int pos = std::max (1, contour.firstCenter); pos < N; )
    {
        marks.push_back (pos);
        pos += sourcePeriodAt (pos);
    }
    if (marks.empty())
    {
        output.makeCopyOf (input);
        return output;
    }

    double tS = (double) marks.front() / timeScale;   // first output position
    size_t mi = 0;

    while (tS < (double) outN)
    {
        const int outCentre = (int) tS;
        const int inTime = (int) (tS * timeScale);

        // Nearest analysis mark to the time-mapped input position.
        while (mi + 1 < marks.size()
               && std::abs (marks[mi + 1] - inTime) <= std::abs (marks[mi] - inTime))
            ++mi;
        while (mi > 0 && std::abs (marks[mi - 1] - inTime) < std::abs (marks[mi] - inTime))
            --mi;
        const int a = marks[mi];

        const int Pa = sourcePeriodAt (a);

        // Output period (pitch) = strength-blended target, in the MIDI domain.
        double Ps = (double) Pa;
        if (voiced[(size_t) outCentre])
        {
            const float eff = srcMidi[(size_t) outCentre]
                            + strength * (tgtMidi[(size_t) outCentre] - srcMidi[(size_t) outCentre]);
            const float effHz = midiToHz (eff);
            if (effHz > 0.0f)
                Ps = std::clamp ((double) sr / effHz, (double) minP, (double) (2 * maxP));
        }

        const int half = std::clamp ((int) std::ceil (std::max ((double) Pa, Ps)),
                                     minP, 3 * maxP);

        // Formant factor: 1 = grain copied as-is (formants preserved); otherwise
        // the grain is read at rate rf, which scales its spectral envelope by rf.
        // rf = (pitch ratio)^(1 - formantPreserve): at 0 the formants move fully
        // with the pitch (classic resampled "chipmunk"/"giant" timbre).
        const bool   moveFormants = formantMove > 1.0e-4f && std::abs ((double) Pa - Ps) > 1.0e-6;
        const double rf = moveFormants
                        ? std::clamp (std::pow ((double) Pa / Ps, (double) formantMove), 0.25, 4.0)
                        : 1.0;

        for (int j = -half; j <= half; ++j)
        {
            const int out = outCentre + j;
            if (out < 0 || out >= outN)
                continue;

            float s;
            if (! moveFormants)
            {
                const int in = a + j;
                if (in < 0 || in >= N)
                    continue;
                s = x[in];
            }
            else
            {
                const double inPos = (double) a + (double) j * rf;
                const int    i0 = (int) std::floor (inPos);
                if (i0 < 0 || i0 + 1 >= N)
                    continue;
                const float frac = (float) (inPos - (double) i0);
                s = x[i0] + frac * (x[i0 + 1] - x[i0]);
            }

            const float w = 0.5f * (1.0f + std::cos (juce::MathConstants<float>::pi * (float) j / (float) half));
            acc[(size_t) out]  += s * w;
            norm[(size_t) out] += w;
        }

        tS += Ps;
    }

    // 6) Normalize OLA and write out.
    float* y = output.getWritePointer (0);
    for (int n = 0; n < outN; ++n)
        y[n] = norm[(size_t) n] > 1.0e-6f ? acc[(size_t) n] / norm[(size_t) n] : 0.0f;

    return output;
}
