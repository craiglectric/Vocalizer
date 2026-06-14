#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <array>

//==============================================================================
// RenderedAudio — one finished render: mono float audio at the host sample rate.
// Reference-counted so the audio thread can hold it while the message thread
// hands over a replacement (CLAUDE.md §4.7 atomic pointer swap).
//==============================================================================
struct RenderedAudio : public juce::ReferenceCountedObject
{
    using Ptr = juce::ReferenceCountedObjectPtr<RenderedAudio>;

    juce::AudioBuffer<float> buffer;   // mono
    double sampleRate = 0.0;
};

//==============================================================================
// AuditionPlayer — real-time-safe sample playback voice (CLAUDE.md §4.5).
//
// The audio thread only READS the active buffer; it never allocates, locks, or
// frees. New renders arrive via a lock-free single-slot handoff; the buffer the
// audio thread retires is pushed to a lock-free queue that the message thread
// drains in collectGarbage(). All ref-count inc/dec happen off the audio thread.
//==============================================================================
class AuditionPlayer
{
public:
    AuditionPlayer() = default;
    ~AuditionPlayer();

    // --- message thread -------------------------------------------------------
    void prepare (double sampleRate, int blockSize);
    void releaseResources();

    // Hand a freshly rendered buffer to the audio thread.
    void setAudio (RenderedAudio::Ptr next);

    // Free buffers the audio thread has retired. Call periodically (e.g. a UI
    // timer). Cheap and safe to call when there's nothing to free.
    void collectGarbage();

    void play();
    void stop();
    void togglePlay();
    void setLooping (bool shouldLoop) noexcept { looping.store (shouldLoop); }

    bool  isPlaying()  const noexcept { return playing.load(); }
    bool  isLooping()  const noexcept { return looping.load(); }
    bool  hasAudio()   const noexcept { return lengthForUi.load() > 0; }
    int   getLengthSamples() const noexcept { return lengthForUi.load(); }
    float getPlayheadNormalized() const noexcept;

    // --- audio thread ---------------------------------------------------------
    // Mixes the audition voice into `output` (assumed already cleared). Picks up
    // any pending handoff first.
    void process (juce::AudioBuffer<float>& output);

private:
    static constexpr int retireCapacity = 16;

    bool pushRetire (RenderedAudio* p) noexcept;   // audio thread

    // Lock-free handoff (message -> audio). Slot owns one reference.
    std::atomic<RenderedAudio*> handoff { nullptr };

    // Lock-free retire queue (audio -> message). Each slot owns one reference.
    std::array<std::atomic<RenderedAudio*>, retireCapacity> retire {};

    // Audio-thread-only state.
    RenderedAudio* active = nullptr;   // owns one reference
    int positionSamples = 0;

    // Shared flags / UI mirrors.
    std::atomic<bool> playing { false };
    std::atomic<bool> looping { false };
    std::atomic<int>  position { 0 };       // mirror of positionSamples for UI
    std::atomic<int>  lengthForUi { 0 };

    double hostSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AuditionPlayer)
};
