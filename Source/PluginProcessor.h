#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "params/Parameters.h"
#include "params/VoicePresets.h"
#include "tts/PiperEngine.h"
#include "dsp/AuditionPlayer.h"
#include "midi/MelodyRecorder.h"
#include <atomic>
#include <vector>

// [[clang::nonblocking]] lets RealtimeSanitizer check the audio path. Only enabled in RTSan
// builds (-DVOCALIZER_RTSAN=ON, recipe in CMakeLists.txt; house pattern from RatXciter).
#if defined(VOCALIZER_RTSAN) && defined(__clang__) && defined(__has_cpp_attribute)
    #if __has_cpp_attribute(clang::nonblocking)
        #define VOCALIZER_NONBLOCKING [[clang::nonblocking]]
    #endif
#endif
#ifndef VOCALIZER_NONBLOCKING
    #define VOCALIZER_NONBLOCKING
#endif

//==============================================================================
// Vocalizer — Phase 2 (CLAUDE.md §8): Generate + audition.
//
// processBlock stays trivially real-time safe: it only clears the buffer, mixes
// the AuditionPlayer (lock-free buffer read), and applies output gain. All
// generation (phonemize -> infer -> resample -> WAV) runs on a worker thread via
// RenderJob, with the finished buffer handed to the audio thread by atomic swap.
// MIDI melody capture + pitch correction arrive in Phase 4.
//==============================================================================
class VocalizerAudioProcessor : public juce::AudioProcessor
{
public:
    VocalizerAudioProcessor();
    ~VocalizerAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) VOCALIZER_NONBLOCKING override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    //==============================================================================
    const juce::String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    //==============================================================================
    int getNumPrograms() override    { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // --- UI-facing API (message thread) ---------------------------------------
    void setText (const juce::String& newText);
    juce::String getText() const;

    void generate();                                  // kicks a RenderJob
    bool isRendering() const noexcept  { return rendering.load(); }
    float getProgress() const noexcept { return progress.load(); }
    juce::String getStatusMessage() const;

    // Transport.
    void play()       { audition.play(); }
    void stop()       { audition.stop(); }
    void togglePlay() { audition.togglePlay(); }
    bool isPlaying() const noexcept { return audition.isPlaying(); }
    bool hasRenderedAudio() const noexcept { return audition.hasAudio(); }
    void setLooping (bool s) { audition.setLooping (s); }
    bool isLooping() const noexcept { return audition.isLooping(); }
    float getPlayheadNormalized() const noexcept { return audition.getPlayheadNormalized(); }

    // Waveform thumbnail (regenerated each render; bumps the generation counter).
    int getRenderGeneration() const noexcept { return renderGeneration.load(); }
    const std::vector<float>& getThumbnail() const noexcept { return thumbnail; }

    // The temp WAV of the last COMPLETED render (Phase 3 drag-to-track). Each
    // render gets a unique filename so DAWs don't reuse a previously-imported
    // clip with the same path.
    juce::File getRenderedWavFile() const { return lastRenderedWav; }

    // Voice presets (CLAUDE.md §5).
    const juce::Array<VoicePreset>& getVoicePresets() const { return presets; }
    // Filename of the voice the engine currently has loaded (for tests/diagnostics).
    juce::String getLoadedVoiceFileName() const;
    // Push the selected preset's default rate/autotune into the APVTS params.
    void applyVoiceDefaults (int presetIndex);

    // MIDI melody capture (CLAUDE.md §4.3).
    void setMelodyArmed (bool a) { melody.setArmed (a); }
    bool isMelodyArmed() const   { return melody.isArmed(); }
    void clearMelody()           { melody.clear(); }
    bool hasMelody() const        { return melody.hasMelody(); }
    Melody getMelodySnapshot() const { return melody.snapshot(); }

    // Pump message-thread housekeeping from the editor timer (buffer GC).
    void serviceMessageThread() { audition.collectGarbage(); }

    //==============================================================================
    juce::AudioProcessorValueTreeState apvts;

private:
    bool ensureEngineReady();                         // worker thread
    void onRenderComplete (RenderedAudio::Ptr);       // message thread
    void onRenderFailed (const juce::String&);        // message thread

    // Engine + worker pool.
    std::unique_ptr<PiperEngine> engine;
    juce::CriticalSection        engineLock;
    juce::ThreadPool             renderPool { 1 };

    // Voice presets + the model the next render should use (set under engineLock).
    juce::Array<VoicePreset> presets;
    juce::File               desiredVoiceJson;   // guarded by engineLock

    // Audition playback.
    AuditionPlayer audition;
    double         currentSampleRate = 44100.0;

    // MIDI melody capture.
    MelodyRecorder melody;

    // Render state.
    std::atomic<bool>  rendering { false };
    std::atomic<float> progress  { 0.0f };
    std::atomic<int>   renderGeneration { 0 };
    juce::String       statusMessage { "ready" };
    juce::CriticalSection statusLock;

    // Text input (message thread).
    juce::String text;

    // Waveform thumbnail (message thread only).
    std::vector<float> thumbnail;

    // Per-instance temp dir. Each render writes a uniquely-named WAV so DAWs
    // never reuse a previously-imported clip with the same path.
    juce::File tempDir;
    juce::File lastRenderedWav;   // last COMPLETED render (drag source), message thread
    int        renderCounter = 0;

    // Cached output-gain param.
    std::atomic<float>* outputGainParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalizerAudioProcessor)
};
