#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include "../tts/PiperEngine.h"
#include "../dsp/AuditionPlayer.h"
#include "../dsp/PitchCorrector.h"
#include "../midi/MelodyRecorder.h"
#include <functional>

//==============================================================================
// RenderJob — the offline Generate pipeline on a worker thread (CLAUDE.md §4.7).
//
// Phase 2 pipeline: ensure voice loaded -> phonemize + infer (PiperEngine) ->
// resample to host SR -> write temp WAV -> hand the buffer to the audio thread.
// Pitch tracking / correction (Phase 4) slot in between resample and handoff.
//
// Cancellable via the ThreadPoolJob mechanism; progress is reported through a
// callback (writes an atomic on the processor); completion/failure are
// marshalled to the message thread.
//==============================================================================
class RenderJob : public juce::ThreadPoolJob
{
public:
    struct Callbacks
    {
        std::function<bool()>                     ensureEngineReady; // worker thread
        std::function<void (float)>               onProgress;        // worker thread
        std::function<void (RenderedAudio::Ptr)>  onComplete;        // message thread
        std::function<void (juce::String)>        onFailed;          // message thread
    };

    // Retune configuration resolved at Generate time (CLAUDE.md §4.3/§4.4).
    struct RetuneConfig
    {
        bool                   autotuneOn = true;
        float                  speakingRate = 1.0f;   // 0.5..2.0x
        PitchCorrector::Params pitch;
        Melody                 melody;                 // captured timeline
    };

    RenderJob (PiperEngine& engine,
               juce::String text,
               double hostSampleRate,
               juce::File tempWavFile,
               RetuneConfig retune,
               Callbacks callbacks);

    JobStatus runJob() override;

private:
    void reportProgress (float p);
    void finishOnMessageThread (RenderedAudio::Ptr result, juce::String error);

    PiperEngine& engine;
    juce::String text;
    double       hostSampleRate;
    juce::File   tempWav;
    RetuneConfig retune;
    Callbacks    cb;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RenderJob)
};
