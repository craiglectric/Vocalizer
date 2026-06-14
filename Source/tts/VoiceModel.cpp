#include "VoiceModel.h"

bool VoiceModel::loadConfig (const juce::File& jsonFile)
{
    if (! jsonFile.existsAsFile())
        return false;

    auto parsed = juce::JSON::parse (jsonFile.loadFileAsString());
    if (! parsed.isObject())
        return false;

    configFile = jsonFile;

    // "<name>.onnx.json" -> "<name>.onnx"
    onnxFile = jsonFile.getParentDirectory()
                   .getChildFile (jsonFile.getFileName().dropLastCharacters (5)); // ".json"

    // audio.sample_rate
    if (auto* audio = parsed.getProperty ("audio", {}).getDynamicObject())
        sampleRate = (int) audio->getProperty ("sample_rate");

    // espeak.voice
    if (auto* espeak = parsed.getProperty ("espeak", {}).getDynamicObject())
        espeakVoice = espeak->getProperty ("voice").toString();

    // inference.{noise_scale,length_scale,noise_w}
    if (auto* inf = parsed.getProperty ("inference", {}).getDynamicObject())
    {
        noiseScale  = (float) (double) inf->getProperty ("noise_scale");
        lengthScale = (float) (double) inf->getProperty ("length_scale");
        noiseW      = (float) (double) inf->getProperty ("noise_w");
    }

    if (parsed.hasProperty ("num_speakers"))
        numSpeakers = (int) parsed.getProperty ("num_speakers", 1);

    // phoneme_id_map: { "<phoneme>": [id, ...], ... }
    phonemeIdMap.clear();
    if (auto* idMap = parsed.getProperty ("phoneme_id_map", {}).getDynamicObject())
    {
        for (const auto& prop : idMap->getProperties())
        {
            const juce::String key = prop.name.toString();
            if (key.isEmpty())
                continue;

            // The key is a single Unicode codepoint encoded as UTF-8.
            const char32_t phoneme = (char32_t) *key.toUTF32().getAddress();

            std::vector<std::int64_t> ids;
            if (const auto* arr = prop.value.getArray())
                for (const auto& v : *arr)
                    ids.push_back ((std::int64_t) (int) v);

            if (! ids.empty())
                phonemeIdMap[phoneme] = std::move (ids);
        }
    }

    return isValid();
}
