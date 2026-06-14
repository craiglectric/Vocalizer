#pragma once

#include <juce_core/juce_core.h>
#include "VoiceModel.h"
#include <vector>
#include <cstdint>

//==============================================================================
// Phonemizer — text -> model phoneme ids, via espeak-ng (G2P) + piper-phonemize
// (phoneme -> id mapping). GPLv3 path; see CLAUDE.md §2 (decision: option a).
//
// espeak-ng is a process-global singleton, so initEspeak() runs once and is
// thread-safe. All of this is worker-thread work — never call from the audio
// thread (CLAUDE.md §9).
//==============================================================================
class Phonemizer
{
public:
    // Initialise espeak-ng. `espeakDataDir` is the directory CONTAINING the
    // "espeak-ng-data" folder (espeak appends the folder name itself). Safe to
    // call repeatedly; only the first call does work. Returns false on failure.
    static bool initEspeak (const juce::File& espeakDataDir);

    // Phonemize `text` into per-sentence id sequences, ready for PiperEngine.
    // Requires initEspeak() to have succeeded. Returns one inner vector per
    // sentence espeak detects.
    static std::vector<std::vector<std::int64_t>>
        textToPhonemeIds (const juce::String& text, const VoiceModel& voice);
};
