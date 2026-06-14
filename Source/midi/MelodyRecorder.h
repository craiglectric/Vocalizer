#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <array>
#include <vector>

//==============================================================================
// Melody — a captured pitch timeline (note numbers over time). Built on the
// message thread from MelodyRecorder's raw events and consumed by PitchCorrector
// as the retune target (CLAUDE.md §4.3).
//==============================================================================
struct MelodyNote
{
    double startSec = 0.0;
    double endSec   = 0.0;
    int    note     = -1;   // MIDI note number
};

struct Melody
{
    std::vector<MelodyNote> notes;
    double durationSec = 0.0;

    bool empty() const { return notes.empty(); }

    // MIDI note active at `sec`, or -1 if none.
    int noteAt (double sec) const;
};

//==============================================================================
// MelodyRecorder — captures the MIDI melody contour (CLAUDE.md §4.3). The audio
// thread writes raw note on/off events with sample-accurate timestamps into a
// lock-free ring; the message thread snapshots them into a Melody. Arming starts
// a fresh capture from t=0; the internal sample clock advances only while armed,
// so timing is captured whether the host transport runs or notes are played live.
//==============================================================================
class MelodyRecorder
{
public:
    void prepare (double sampleRate);

    // --- audio thread ---------------------------------------------------------
    void processBlock (const juce::MidiBuffer& midi, int numSamples);

    // --- message thread -------------------------------------------------------
    void setArmed (bool shouldArm);
    bool isArmed() const noexcept { return armed.load(); }
    void clear();
    bool hasMelody() const noexcept { return writeIndex.load() > 0; }

    Melody snapshot() const;

    // Restore a melody (e.g. from saved plugin state). Not real-time safe; call
    // on the message thread while audio is stopped.
    void setMelody (const Melody& m);

private:
    struct RawEvent
    {
        std::int64_t timeSamples = 0;
        int          note = 0;
        bool         isOn = false;
    };

    static constexpr int capacity = 8192;   // 4096 notes max

    std::array<RawEvent, capacity> events {};
    std::atomic<int>          writeIndex { 0 };
    std::atomic<bool>         armed { false };
    std::atomic<std::int64_t> clock { 0 };          // samples since arm
    std::atomic<std::int64_t> lengthSamples { 0 };
    double sampleRate = 48000.0;
};
