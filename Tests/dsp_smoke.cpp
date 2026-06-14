// Phase 4 DSP verification (no listening required):
//   1) PitchCorrector forces the output f0 onto the target MIDI note.
//   2) Formants are preserved — the spectral-envelope peak stays put when the
//      pitch is shifted (a naive resample would move it).
//
//   cmake -B build -DVOCALIZER_BUILD_TOOLS=ON
//   cmake --build build --target dsp_smoke && ./build/dsp_smoke

#include "dsp/PitchTracker.h"
#include "dsp/PitchCorrector.h"
#include "midi/MelodyRecorder.h"
#include <juce_dsp/juce_dsp.h>
#include <cstdio>
#include <vector>
#include <algorithm>
#include <cmath>

namespace
{
    constexpr double SR = 48000.0;

    // A harmonic-rich "vowel": harmonics of f0 shaped by two fixed formants.
    juce::AudioBuffer<float> makeVowel (float f0, double seconds,
                                        float F1 = 650.0f, float F2 = 1080.0f)
    {
        const int N = (int) (seconds * SR);
        juce::AudioBuffer<float> b (1, N);
        float* x = b.getWritePointer (0);

        auto formant = [F1, F2] (float f)
        {
            const float r1 = 1.0f / (1.0f + std::pow ((f - F1) / 80.0f,  2.0f));
            const float r2 = 0.7f / (1.0f + std::pow ((f - F2) / 110.0f, 2.0f));
            return r1 + r2 + 0.03f;
        };

        const int maxK = (int) (SR * 0.5 / f0);
        for (int n = 0; n < N; ++n)
        {
            float s = 0.0f;
            for (int k = 1; k <= maxK; ++k)
                s += formant ((float) k * f0) * std::sin (2.0f * juce::MathConstants<float>::pi
                                                          * (float) k * f0 * (float) n / (float) SR);
            x[n] = s;
        }
        b.applyGain (0.5f / juce::jmax (1.0e-3f, b.getMagnitude (0, N)));
        return b;
    }

    float medianF0 (const juce::AudioBuffer<float>& b)
    {
        auto c = PitchTracker::analyze (b.getReadPointer (0), b.getNumSamples(), SR);
        std::vector<float> v;
        for (float f : c.f0) if (f > 0.0f) v.push_back (f);
        if (v.empty()) return 0.0f;
        std::sort (v.begin(), v.end());
        return v[v.size() / 2];
    }

    // Dominant spectral peak (Hz) in [lo,hi], from an averaged magnitude spectrum.
    float spectralPeak (const juce::AudioBuffer<float>& b, float lo, float hi)
    {
        const int order = 12, fft = 1 << order;          // 4096
        juce::dsp::FFT f (order);
        std::vector<float> mag ((size_t) fft / 2, 0.0f);
        std::vector<float> fd ((size_t) fft * 2, 0.0f);

        const int N = b.getNumSamples();
        const float* x = b.getReadPointer (0);
        int frames = 0;
        for (int start = 0; start + fft < N; start += fft / 2)
        {
            std::fill (fd.begin(), fd.end(), 0.0f);
            for (int i = 0; i < fft; ++i)
            {
                const float w = 0.5f * (1.0f - std::cos (2.0f * juce::MathConstants<float>::pi * i / (fft - 1)));
                fd[(size_t) i] = x[start + i] * w;
            }
            f.performRealOnlyForwardTransform (fd.data());
            for (int k = 0; k < fft / 2; ++k)
            {
                const float re = fd[(size_t) (2 * k)], im = fd[(size_t) (2 * k + 1)];
                mag[(size_t) k] += std::sqrt (re * re + im * im);
            }
            ++frames;
        }
        if (frames == 0) return 0.0f;

        const float binHz = (float) SR / (float) fft;
        int bestBin = 0; float best = -1.0f;
        for (int k = 1; k < fft / 2; ++k)
        {
            const float fHz = k * binHz;
            if (fHz < lo || fHz > hi) continue;
            if (mag[(size_t) k] > best) { best = mag[(size_t) k]; bestBin = k; }
        }
        return bestBin * binHz;
    }

    int fails = 0;
    void check (bool ok, const char* what)
    {
        std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what);
        if (! ok) ++fails;
    }
}

int main()
{
    // --- Test 1: pitch accuracy -------------------------------------------------
    std::printf ("== pitch accuracy (source 130 Hz -> target note) ==\n");
    for (int note : { 55, 60, 64, 67 })   // G3, C4, E4, G4
    {
        auto src = makeVowel (130.0f, 1.2);

        Melody m;
        m.durationSec = 1.2;
        m.notes.push_back ({ 0.0, 1.2, note });

        PitchCorrector::Params p;
        p.mode = 0; p.strength = 1.0f; p.retuneSpeedMs = 0.0f;

        auto out = PitchCorrector::process (src, SR, m, p);
        const float got = medianF0 (out);
        const float want = PitchCorrector::midiToHz ((float) note);
        const float errPct = std::abs (got - want) / want * 100.0f;

        char msg[160];
        std::snprintf (msg, sizeof msg, "note %d: target %.1f Hz, measured %.1f Hz (%.1f%%)",
                       note, want, got, errPct);
        check (errPct < 4.0f, msg);
    }

    // --- Test 2: formant preservation ------------------------------------------
    std::printf ("== formant preservation (formant should NOT move with pitch) ==\n");
    {
        auto src = makeVowel (150.0f, 1.5, 650.0f, 1080.0f);
        const float srcF0   = medianF0 (src);
        const float srcForm = spectralPeak (src, 400.0f, 900.0f);

        // Shift down ~7 semitones to 100 Hz; formant peak must stay near 650.
        Melody m; m.durationSec = 1.5;
        const int note = (int) std::lround (PitchCorrector::hzToMidi (100.0f));
        m.notes.push_back ({ 0.0, 1.5, note });

        PitchCorrector::Params p; p.mode = 0; p.strength = 1.0f; p.retuneSpeedMs = 0.0f;
        auto out = PitchCorrector::process (src, SR, m, p);

        const float outF0   = medianF0 (out);
        const float outForm = spectralPeak (out, 400.0f, 900.0f);

        std::printf ("   source: f0 %.1f Hz, formant %.0f Hz\n", srcF0, srcForm);
        std::printf ("   output: f0 %.1f Hz, formant %.0f Hz (target f0 %.1f)\n",
                     outF0, outForm, PitchCorrector::midiToHz ((float) note));

        check (std::abs (outF0 - PitchCorrector::midiToHz ((float) note)) / outF0 < 0.06f,
               "pitch shifted to ~100 Hz");
        check (std::abs (outForm - srcForm) < 130.0f,
               "formant peak preserved (within 130 Hz)");
    }

    // --- Test 3: scale-snap -----------------------------------------------------
    std::printf ("== scale-snap (C major) ==\n");
    {
        // 64.4 (just above E) should snap to E(64) in C major; 61.5 -> 62 (D) etc.
        const float a = PitchCorrector::snapToScale (64.4f, 0, 1);  // C major
        const float b = PitchCorrector::snapToScale (61.6f, 0, 1);
        char msg[120];
        std::snprintf (msg, sizeof msg, "snap 64.4->%.0f (want 64), 61.6->%.0f (want 62)", a, b);
        check (std::abs (a - 64.0f) < 0.5f && std::abs (b - 62.0f) < 0.5f, msg);
    }

    std::printf ("\n%s (%d failure%s)\n", fails == 0 ? "ALL PASS" : "FAILURES", fails, fails == 1 ? "" : "s");
    return fails == 0 ? 0 : 1;
}
