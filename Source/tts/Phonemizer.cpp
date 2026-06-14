#include "Phonemizer.h"

#include <espeak-ng/speak_lib.h>
#include <piper-phonemize/phonemize.hpp>
#include <piper-phonemize/phoneme_ids.hpp>

#include <atomic>
#include <memory>

namespace
{
    std::atomic<bool> espeakReady { false };
}

bool Phonemizer::initEspeak (const juce::File& espeakDataDir)
{
    if (espeakReady.load())
        return true;

    // espeak_Initialize wants the directory that CONTAINS "espeak-ng-data".
    // Tolerate being handed the data folder itself by stepping up a level.
    juce::File dir = espeakDataDir;
    if (dir.getFileName() == "espeak-ng-data")
        dir = dir.getParentDirectory();

    const int rate = espeak_Initialize (AUDIO_OUTPUT_SYNCHRONOUS,
                                        /*buflength*/ 0,
                                        dir.getFullPathName().toRawUTF8(),
                                        /*options*/ 0);
    const bool ok = (rate > 0);
    espeakReady.store (ok);
    return ok;
}

std::vector<std::vector<std::int64_t>>
Phonemizer::textToPhonemeIds (const juce::String& text, const VoiceModel& voice)
{
    std::vector<std::vector<std::int64_t>> result;

    if (! espeakReady.load() || text.trim().isEmpty())
        return result;

    // 1) text -> phonemes (per sentence) via espeak-ng.
    piper::eSpeakPhonemeConfig eConfig;
    eConfig.voice = voice.espeakVoice.toStdString();

    std::vector<std::vector<piper::Phoneme>> sentences;
    piper::phonemize_eSpeak (text.toStdString(), eConfig, sentences);

    // 2) phonemes -> ids via the voice's trained map.
    auto idMap = std::make_shared<piper::PhonemeIdMap> (voice.phonemeIdMap);

    for (auto& sentence : sentences)
    {
        piper::PhonemeIdConfig idConfig;
        idConfig.phonemeIdMap = idMap;   // bos/eos/pad interspersing on by default

        std::vector<piper::PhonemeId>      ids;
        std::map<piper::Phoneme, std::size_t> missing;
        piper::phonemes_to_ids (sentence, idConfig, ids, missing);

        if (! ids.empty())
            result.emplace_back (ids.begin(), ids.end());
    }

    return result;
}
