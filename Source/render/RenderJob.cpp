#include "RenderJob.h"
#include <juce_events/juce_events.h>

RenderJob::RenderJob (PiperEngine& engineToUse,
                      juce::String textToSpeak,
                      double sampleRate,
                      juce::File tempWavFile,
                      RetuneConfig retuneConfig,
                      Callbacks callbacks)
    : juce::ThreadPoolJob ("Vocalizer render"),
      engine (engineToUse),
      text (std::move (textToSpeak)),
      hostSampleRate (sampleRate),
      tempWav (std::move (tempWavFile)),
      retune (std::move (retuneConfig)),
      cb (std::move (callbacks))
{
}

void RenderJob::reportProgress (float p)
{
    if (cb.onProgress)
        cb.onProgress (juce::jlimit (0.0f, 1.0f, p));
}

void RenderJob::finishOnMessageThread (RenderedAudio::Ptr result, juce::String error)
{
    // Copy callbacks out so the lambda doesn't capture `this` (job may be
    // auto-deleted by the pool as soon as runJob returns).
    auto onComplete = cb.onComplete;
    auto onFailed   = cb.onFailed;

    juce::MessageManager::callAsync ([onComplete, onFailed, result, error]
    {
        if (result != nullptr) { if (onComplete) onComplete (result); }
        else                   { if (onFailed)   onFailed (error); }
    });
}

juce::ThreadPoolJob::JobStatus RenderJob::runJob()
{
    reportProgress (0.05f);

    if (cb.ensureEngineReady && ! cb.ensureEngineReady())
    {
        finishOnMessageThread (nullptr, "Voice model failed to load.");
        return jobHasFinished;
    }
    if (shouldExit()) return jobHasFinished;

    // 1) Text -> speech at the voice's native sample rate.
    reportProgress (0.15f);
    auto speech = engine.synthesize (text, retune.speakingRate);
    if (shouldExit()) return jobHasFinished;

    if (speech.getNumSamples() == 0)
    {
        finishOnMessageThread (nullptr, "Nothing to synthesize.");
        return jobHasFinished;
    }

    // 2) Resample to the host sample rate (CLAUDE.md §9).
    reportProgress (0.6f);
    const double srcRate = (double) engine.getSampleRate();

    auto result = new RenderedAudio();
    result->sampleRate = hostSampleRate;

    if (juce::approximatelyEqual (srcRate, hostSampleRate))
    {
        result->buffer.makeCopyOf (speech);
    }
    else
    {
        const double ratio = srcRate / hostSampleRate;            // input per output
        const int inLen  = speech.getNumSamples();
        const int outLen = (int) std::ceil ((double) inLen / ratio);

        result->buffer.setSize (1, outLen);
        juce::LagrangeInterpolator interp;
        interp.reset();
        interp.process (ratio, speech.getReadPointer (0),
                        result->buffer.getWritePointer (0), outLen);
    }

    if (shouldExit())
    {
        delete result;
        return jobHasFinished;
    }

    // 2b) Retune toward the MIDI melody / scale (CLAUDE.md §4.3/§4.4). Skipped
    //     when autotune is off, or in MIDI-follow mode with no captured melody
    //     (nothing to follow) — Scale-snap always has a target.
    const bool haveTarget = (retune.pitch.mode == 1) || ! retune.melody.empty();
    if (retune.autotuneOn && haveTarget)
    {
        reportProgress (0.7f);
        auto retuned = PitchCorrector::process (result->buffer, hostSampleRate,
                                                retune.melody, retune.pitch);
        if (retuned.getNumSamples() > 0)
            result->buffer = std::move (retuned);

        if (shouldExit())
        {
            delete result;
            return jobHasFinished;
        }
    }

    // 3) Write the temp WAV (host SR, 24-bit) for audition continuity + Phase 3
    //    drag-to-track. Failure here is non-fatal for audition.
    reportProgress (0.85f);
    {
        tempWav.deleteFile();
        juce::WavAudioFormat wav;
        if (auto os = std::unique_ptr<juce::FileOutputStream> (tempWav.createOutputStream()))
        {
            if (auto* w = wav.createWriterFor (os.get(), hostSampleRate, 1, 24, {}, 0))
            {
                os.release();
                std::unique_ptr<juce::AudioFormatWriter> writer (w);
                writer->writeFromAudioSampleBuffer (result->buffer, 0,
                                                    result->buffer.getNumSamples());
            }
        }
    }

    // 4) Hand off to the audio thread (via the message thread).
    reportProgress (1.0f);
    finishOnMessageThread (RenderedAudio::Ptr (result), {});
    return jobHasFinished;
}
