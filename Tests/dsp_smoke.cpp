// Phase 4 DSP verification (no listening required):
//   1) PitchCorrector forces the output f0 onto the target MIDI note.
//   2) Formants are preserved — the spectral-envelope peak stays put when the
//      pitch is shifted (a naive resample would move it).
//   2b) FORMANT (formantPreserve) < 1 makes the formants follow the pitch.
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

    // --- Test 2b: FORMANT knob at 0 — formants follow the pitch ------------------
    std::printf ("== formant preserve 0 (formant SHOULD move with pitch) ==\n");
    {
        auto src = makeVowel (150.0f, 1.5, 650.0f, 1080.0f);
        const float srcForm = spectralPeak (src, 300.0f, 900.0f);

        Melody m; m.durationSec = 1.5;
        const int note = (int) std::lround (PitchCorrector::hzToMidi (100.0f));
        m.notes.push_back ({ 0.0, 1.5, note });
        const float wantF0 = PitchCorrector::midiToHz ((float) note);

        PitchCorrector::Params p; p.mode = 0; p.strength = 1.0f; p.retuneSpeedMs = 0.0f;
        p.formantPreserve = 0.0f;
        auto out = PitchCorrector::process (src, SR, m, p);

        const float outF0   = medianF0 (out);
        const float outForm = spectralPeak (out, 300.0f, 900.0f);
        const float wantForm = srcForm * wantF0 / 150.0f;     // envelope scaled by pitch ratio

        std::printf ("   output: f0 %.1f Hz, formant %.0f Hz (expected ~%.0f)\n",
                     outF0, outForm, wantForm);

        check (std::abs (outF0 - wantF0) / outF0 < 0.06f, "pitch still shifted to ~100 Hz");
        check (std::abs (outForm - wantForm) < 100.0f && outForm < srcForm - 100.0f,
               "formant peak moved down with the pitch");

        // Half-way: the formant lands between preserved and fully moved.
        p.formantPreserve = 0.5f;
        auto half = PitchCorrector::process (src, SR, m, p);
        const float halfForm = spectralPeak (half, 300.0f, 900.0f);
        std::printf ("   preserve 0.5: formant %.0f Hz\n", halfForm);
        check (halfForm <= srcForm && halfForm > outForm + 1.0f,
               "formant preserve 0.5 sits between 0 and 1");
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

    // --- Test 4: SCALE-SNAP survives SPEED > 0 (audit 2026-10-08 HIGH) --------
    // Words = voiced vowels at off-scale pitches separated by silent gaps. The
    // glide used to run toward MIDI 0 through every unvoiced sample, so each word
    // scooped up from far below and only ~half the voiced frames sat on the scale.
    std::printf ("== scale-snap with SPEED 20 ms (words separated by pauses) ==\n");
    {
        const float wordMidi[] = { 57.4f, 60.6f, 64.4f, 62.5f, 59.3f, 65.6f, 61.4f, 58.6f };
        const double wordSec = 0.16, gapSec = 0.07;
        juce::AudioBuffer<float> src (1, (int) (SR * (wordSec + gapSec) * 8 + SR * 0.1));
        src.clear();
        int pos = (int) (SR * 0.05);
        for (float wm : wordMidi)
        {
            auto w = makeVowel (PitchCorrector::midiToHz (wm), wordSec);
            const int L = w.getNumSamples(), fade = (int) (SR * 0.005);
            for (int i = 0; i < L; ++i)
            {
                const float g = std::min (1.0f, (float) std::min (i, L - 1 - i) / (float) fade);
                src.setSample (0, pos + i, w.getSample (0, i) * g);
            }
            pos += L + (int) (SR * gapSec);
        }

        PitchCorrector::Params p; p.mode = 1; p.key = 0; p.scale = 1; p.retuneSpeedMs = 20.0f;
        auto out = PitchCorrector::process (src, SR, Melody{}, p);

        auto c = PitchTracker::analyze (out.getReadPointer (0), out.getNumSamples(), SR);
        const int cmaj[] = { 0, 2, 4, 5, 7, 9, 11 };
        int voicedN = 0, onScale = 0;
        for (float f : c.f0)
        {
            if (f <= 0.0f) continue;
            ++voicedN;
            const float m = PitchCorrector::hzToMidi (f);
            const int   r = (int) std::lround (m);
            const bool inScale = std::find (std::begin (cmaj), std::end (cmaj), ((r % 12) + 12) % 12) != std::end (cmaj);
            if (inScale && std::abs (m - (float) r) <= 0.25f) ++onScale;
        }
        const float pct = voicedN > 0 ? 100.0f * (float) onScale / (float) voicedN : 0.0f;
        char msg[160];
        std::snprintf (msg, sizeof msg, "C major, SPEED 20 ms: %.1f%% of %d voiced frames within 25 c of the scale (want >= 85%%)",
                       pct, voicedN);
        check (voicedN > 20 && pct >= 85.0f, msg);
    }

    // --- Test 5: large downward retunes land (audit 2026-10-08 HIGH) -------------
    // A G3 voice sent an octave (G2), an octave + semitone (F#2) and ~1.6 octaves
    // (C2) down used to come back at G3: grains of half-length max(Pa, Ps) re-
    // stitched the source when Ps ~ k * Pa. Up-shifts must stay exact.
    std::printf ("== large down-shifts (source G3 196 Hz) ==\n");
    for (int note : { 43, 42, 36, 48, 62, 67 })
    {
        auto src = makeVowel (PitchCorrector::midiToHz (55.0f), 1.2);
        Melody m; m.durationSec = 1.2;
        m.notes.push_back ({ 0.0, 1.2, note });
        PitchCorrector::Params p; p.mode = 0; p.strength = 1.0f; p.retuneSpeedMs = 0.0f;
        auto out = PitchCorrector::process (src, SR, m, p);
        const float got = medianF0 (out);
        const float cents = got > 0.0f ? 100.0f * (PitchCorrector::hzToMidi (got) - (float) note) : 9999.0f;
        char msg[160];
        std::snprintf (msg, sizeof msg, "G3 -> note %d: measured %.1f Hz (%+.0f c, want within +-20)", note, got, cents);
        check (std::abs (cents) <= 20.0f, msg);
    }

    std::printf ("\n%s (%d failure%s)\n", fails == 0 ? "ALL PASS" : "FAILURES", fails, fails == 1 ? "" : "s");
    return fails == 0 ? 0 : 1;
}
