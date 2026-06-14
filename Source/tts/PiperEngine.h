#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "VoiceModel.h"
#include <memory>

//==============================================================================
// PiperEngine — owns the ONNX Runtime session for one Piper voice and turns
// text into a mono float audio buffer at the voice's native sample rate.
//
// Heavy: session creation and inference are WORKER-THREAD ONLY and the session
// is cached per voice (CLAUDE.md §4.2 / §9). ONNX Runtime types are hidden
// behind a pimpl so this header stays light.
//==============================================================================
class PiperEngine
{
public:
    PiperEngine();
    ~PiperEngine();

    // Load the voice config + ONNX model and initialise espeak-ng.
    //   onnxJson     : path to "<name>.onnx.json" (the .onnx sits beside it)
    //   espeakDataDir: directory containing "espeak-ng-data"
    bool loadVoice (const juce::File& onnxJson, const juce::File& espeakDataDir);

    bool isLoaded() const { return loaded; }
    int  getSampleRate() const { return voice.sampleRate; }
    const VoiceModel& getVoice() const { return voice; }

    // Synthesize speech for `text` -> mono float buffer at getSampleRate().
    // Raw model output (not normalised). Returns an empty buffer on failure.
    // `speakingRate` > 1 speaks faster (scales the model length-scale).
    juce::AudioBuffer<float> synthesize (const juce::String& text, float speakingRate = 1.0f);

private:
    VoiceModel voice;
    bool       loaded = false;

    struct Impl;
    std::unique_ptr<Impl> impl;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PiperEngine)
};
