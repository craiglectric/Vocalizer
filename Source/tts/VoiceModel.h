#pragma once

#include <juce_core/juce_core.h>
#include <map>
#include <vector>
#include <cstdint>

//==============================================================================
// VoiceModel — a Piper voice = an ONNX VITS model (.onnx) + its config (.json).
// This loads the config: sample rate, espeak voice id, inference scales, and
// the phoneme -> id map the model was trained with. The heavy .onnx itself is
// opened by PiperEngine (ONNX Runtime), not here.
//==============================================================================
class VoiceModel
{
public:
    // Parse a Piper "<name>.onnx.json" config. The sibling "<name>.onnx" is
    // located automatically (drop the trailing ".json"). Returns false if the
    // JSON is unreadable or the .onnx is missing.
    bool loadConfig (const juce::File& jsonFile);

    bool isValid() const { return onnxFile.existsAsFile() && ! phonemeIdMap.empty(); }

    juce::File   configFile;
    juce::File   onnxFile;

    int          sampleRate  = 22050;
    juce::String espeakVoice = "en-us";
    int          numSpeakers = 1;

    // VITS inference scales (Piper "inference" block).
    float noiseScale  = 0.667f;
    float lengthScale = 1.0f;
    float noiseW      = 0.8f;

    // phoneme (Unicode codepoint) -> one or more model input ids.
    // Same shape as piper::PhonemeIdMap, so it binds straight into
    // piper::phonemes_to_ids.
    std::map<char32_t, std::vector<std::int64_t>> phonemeIdMap;
};
