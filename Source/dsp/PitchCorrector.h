#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "../midi/MelodyRecorder.h"

//==============================================================================
// PitchCorrector — offline formant-preserving retune (CLAUDE.md §4.4).
//
// Estimates the speech f0 (PitchTracker), derives a target pitch per frame
// (MIDI-follow against the captured melody, or Scale-snap to Key+Scale), then
// resynthesizes via synthesis-pitch-synchronous OLA (TD-PSOLA style): grain
// content comes from the source at its own period, so the spectral envelope —
// the formants — is preserved while the pitch is forced onto the target. No
// resampling of grains, so no chipmunk effect.
//
// `strength` blends corrected vs original pitch (in the log/MIDI domain);
// `retuneSpeedMs` glides the target (0 = hard snap → robotic).
//==============================================================================
class PitchCorrector
{
public:
    struct Params
    {
        int   mode           = 0;     // 0 = MIDI-follow, 1 = Scale-snap
        int   key            = 0;     // 0..11 (C..B)
        int   scale          = 1;     // index into the scale table
        float retuneSpeedMs  = 20.0f;
        float strength       = 1.0f;  // 0..1
        float formantPreserve = 1.0f; // reserved; PSOLA preserves formants already

        // Clip-sync (CLAUDE.md §4.3 absolute-time mode): time-stretch the speech
        // so the output length == the captured melody length, and follow the
        // notes at their real times — so the rendered clip lines up with the MIDI
        // clip / project tempo. Only applies in MIDI-follow mode with a melody;
        // otherwise the output stays at the natural speech length.
        bool  clipSync       = true;
    };

    // Retune `input` (mono) at `sampleRate` toward `melody` (or the scale).
    // Returns a new mono buffer of the same length.
    static juce::AudioBuffer<float> process (const juce::AudioBuffer<float>& input,
                                             double sampleRate,
                                             const Melody& melody,
                                             const Params& params);

    // --- helpers (exposed for tests) -----------------------------------------
    static float midiToHz (float midi);
    static float hzToMidi (float hz);
    // Snap a (fractional) MIDI value to the nearest note of key+scale.
    static float snapToScale (float midi, int key, int scale);
};
