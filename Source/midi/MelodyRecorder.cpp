#include "MelodyRecorder.h"
#include <algorithm>

//==============================================================================
int Melody::noteAt (double sec) const
{
    for (const auto& n : notes)
        if (sec >= n.startSec && sec < n.endSec)
            return n.note;
    return -1;
}

//==============================================================================
void MelodyRecorder::prepare (double sr)
{
    sampleRate = sr;
}

void MelodyRecorder::processBlock (const juce::MidiBuffer& midi, int numSamples)
{
    if (! armed.load (std::memory_order_acquire))
        return;

    const std::int64_t base = clock.load (std::memory_order_relaxed);

    for (const auto meta : midi)
    {
        // Read the raw bytes: meta.getMessage() builds a juce::MidiMessage, which
        // heap-allocates for anything over 8 bytes (SysEx) — found by RTSan.
        // Same rules as MidiMessage::isNoteOn()/isNoteOff(): 0x90 with velocity 0
        // counts as a note-off.
        if (meta.numBytes < 3)
            continue;

        const auto status = meta.data[0] & 0xf0;
        const bool noteOn  = status == 0x90 && meta.data[2] != 0;
        const bool noteOff = status == 0x80 || (status == 0x90 && meta.data[2] == 0);
        if (! (noteOn || noteOff))
            continue;

        int idx = writeIndex.load (std::memory_order_relaxed);
        if (idx >= capacity)
            break;

        RawEvent e;
        e.timeSamples = base + meta.samplePosition;
        e.note = meta.data[1] & 0x7f;
        e.isOn = noteOn;

        events[(size_t) idx] = e;
        writeIndex.store (idx + 1, std::memory_order_release);
    }

    const std::int64_t newClock = base + numSamples;
    clock.store (newClock, std::memory_order_relaxed);
    lengthSamples.store (newClock, std::memory_order_release);
}

//==============================================================================
void MelodyRecorder::setArmed (bool shouldArm)
{
    if (shouldArm)
    {
        // Start a fresh capture.
        writeIndex.store (0, std::memory_order_release);
        clock.store (0, std::memory_order_relaxed);
        lengthSamples.store (0, std::memory_order_release);
    }
    armed.store (shouldArm, std::memory_order_release);
}

void MelodyRecorder::clear()
{
    armed.store (false, std::memory_order_release);
    writeIndex.store (0, std::memory_order_release);
    clock.store (0, std::memory_order_relaxed);
    lengthSamples.store (0, std::memory_order_release);
}

void MelodyRecorder::setMelody (const Melody& m)
{
    armed.store (false, std::memory_order_release);

    int idx = 0;
    for (const auto& n : m.notes)
    {
        if (idx + 2 > capacity) break;
        events[(size_t) idx++] = { (std::int64_t) std::llround (n.startSec * sampleRate), n.note, true };
        events[(size_t) idx++] = { (std::int64_t) std::llround (n.endSec   * sampleRate), n.note, false };
    }

    clock.store ((std::int64_t) std::llround (m.durationSec * sampleRate), std::memory_order_relaxed);
    lengthSamples.store ((std::int64_t) std::llround (m.durationSec * sampleRate), std::memory_order_release);
    writeIndex.store (idx, std::memory_order_release);
}

//==============================================================================
Melody MelodyRecorder::snapshot() const
{
    Melody melody;

    const int n = writeIndex.load (std::memory_order_acquire);
    const std::int64_t len = lengthSamples.load (std::memory_order_acquire);
    melody.durationSec = (double) len / sampleRate;

    // Pair note-ons with the next note-off of the same number (monophonic-ish:
    // we tolerate overlaps by closing the oldest open note of that number).
    struct Open { std::int64_t start; int note; };
    std::vector<Open> open;

    for (int i = 0; i < n; ++i)
    {
        const auto& e = events[(size_t) i];

        if (e.isOn)
        {
            open.push_back ({ e.timeSamples, e.note });
        }
        else
        {
            for (auto it = open.begin(); it != open.end(); ++it)
            {
                if (it->note == e.note)
                {
                    melody.notes.push_back ({ (double) it->start / sampleRate,
                                              (double) e.timeSamples / sampleRate,
                                              e.note });
                    open.erase (it);
                    break;
                }
            }
        }
    }

    // Close any notes still held at capture end.
    for (const auto& o : open)
        melody.notes.push_back ({ (double) o.start / sampleRate,
                                  (double) len / sampleRate, o.note });

    std::sort (melody.notes.begin(), melody.notes.end(),
               [] (const MelodyNote& a, const MelodyNote& b) { return a.startSec < b.startSec; });

    return melody;
}
