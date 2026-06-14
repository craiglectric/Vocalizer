#include "AuditionPlayer.h"

AuditionPlayer::~AuditionPlayer()
{
    releaseResources();
}

//==============================================================================
void AuditionPlayer::prepare (double sampleRate, int /*blockSize*/)
{
    // SR change invalidates a buffer rendered at the old host rate.
    if (! juce::approximatelyEqual (sampleRate, hostSampleRate))
    {
        playing.store (false);
        if (active != nullptr)
        {
            active->decReferenceCount();   // message thread: audio stopped here
            active = nullptr;
        }
        positionSamples = 0;
        position.store (0);
        lengthForUi.store (0);
    }

    hostSampleRate = sampleRate;
}

void AuditionPlayer::releaseResources()
{
    playing.store (false);

    if (active != nullptr)
    {
        active->decReferenceCount();
        active = nullptr;
    }

    if (auto* p = handoff.exchange (nullptr))
        p->decReferenceCount();

    collectGarbage();
}

//==============================================================================
void AuditionPlayer::setAudio (RenderedAudio::Ptr next)
{
    if (next == nullptr)
        return;

    next->incReferenceCount();                       // ref owned by the handoff slot
    if (auto* old = handoff.exchange (next.get()))   // displaced, never consumed
        old->decReferenceCount();

    // Make the clip visible to the UI / play() immediately (before the audio
    // thread adopts it). The audio thread re-stores the same value on pickup.
    lengthForUi.store (next->buffer.getNumSamples());
    position.store (0);
}

void AuditionPlayer::collectGarbage()
{
    for (auto& slot : retire)
        if (auto* p = slot.exchange (nullptr, std::memory_order_acquire))
            p->decReferenceCount();
}

bool AuditionPlayer::pushRetire (RenderedAudio* p) noexcept
{
    for (auto& slot : retire)
    {
        RenderedAudio* expected = nullptr;
        if (slot.compare_exchange_strong (expected, p,
                                          std::memory_order_release,
                                          std::memory_order_relaxed))
            return true;
    }
    return false;   // queue full (renders are seconds apart — not expected)
}

//==============================================================================
void AuditionPlayer::play()
{
    if (! hasAudio())
        return;

    if (positionSamples >= lengthForUi.load())
    {
        positionSamples = 0;
        position.store (0);
    }
    playing.store (true);
}

void AuditionPlayer::stop()
{
    playing.store (false);
}

void AuditionPlayer::togglePlay()
{
    if (playing.load()) stop();
    else                play();
}

float AuditionPlayer::getPlayheadNormalized() const noexcept
{
    const int len = lengthForUi.load();
    return len > 0 ? juce::jlimit (0.0f, 1.0f, (float) position.load() / (float) len)
                   : 0.0f;
}

//==============================================================================
void AuditionPlayer::process (juce::AudioBuffer<float>& output)
{
    // 1) Pick up a pending render (lock-free), retiring the previous buffer.
    if (auto* incoming = handoff.exchange (nullptr, std::memory_order_acquire))
    {
        RenderedAudio* old = active;
        active = incoming;                 // adopt slot's reference (no count change)
        positionSamples = 0;
        position.store (0);
        lengthForUi.store (active->buffer.getNumSamples());

        if (old != nullptr && ! pushRetire (old))
            pushRetire (old);              // single retry; otherwise leak one buffer
    }

    // 2) Play, if we have audio and the transport is running.
    if (! playing.load() || active == nullptr)
        return;

    const auto& src = active->buffer;
    const int srcLen = src.getNumSamples();
    if (srcLen <= 0)
        return;

    const float* s = src.getReadPointer (0);
    const int numCh = output.getNumChannels();
    const int numSamps = output.getNumSamples();

    int pos = positionSamples;
    const bool loop = looping.load();

    for (int i = 0; i < numSamps; ++i)
    {
        if (pos >= srcLen)
        {
            if (loop) { pos = 0; }
            else      { playing.store (false); break; }
        }

        const float v = s[pos++];
        for (int ch = 0; ch < numCh; ++ch)
            output.addSample (ch, i, v);   // buffer pre-cleared by the processor
    }

    positionSamples = pos;
    position.store (pos, std::memory_order_release);
}
