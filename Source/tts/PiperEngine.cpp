#include "PiperEngine.h"
#include "Phonemizer.h"

#include <onnxruntime_cxx_api.h>
#include <array>
#include <vector>

//==============================================================================
struct PiperEngine::Impl
{
    Ort::Env env { ORT_LOGGING_LEVEL_WARNING, "vocalizer" };
    Ort::SessionOptions options;
    std::unique_ptr<Ort::Session> session;
    bool hasSid = false;   // multi-speaker models expose a "sid" input
};

//==============================================================================
PiperEngine::PiperEngine() : impl (std::make_unique<Impl>()) {}
PiperEngine::~PiperEngine() = default;

//==============================================================================
bool PiperEngine::loadVoice (const juce::File& onnxJson, const juce::File& espeakDataDir)
{
    loaded = false;

    if (! voice.loadConfig (onnxJson))
        return false;

    if (! Phonemizer::initEspeak (espeakDataDir))
        return false;

    impl->options.SetIntraOpNumThreads (1);
    impl->options.SetInterOpNumThreads (1);
    impl->options.SetGraphOptimizationLevel (GraphOptimizationLevel::ORT_ENABLE_ALL);

    try
    {
        impl->session = std::make_unique<Ort::Session> (
            impl->env, voice.onnxFile.getFullPathName().toRawUTF8(), impl->options);
    }
    catch (const Ort::Exception&)
    {
        return false;
    }

    // Discover whether the graph wants a speaker id.
    Ort::AllocatorWithDefaultOptions alloc;
    impl->hasSid = false;
    for (size_t i = 0; i < impl->session->GetInputCount(); ++i)
    {
        auto name = impl->session->GetInputNameAllocated (i, alloc);
        if (std::string (name.get()) == "sid")
            impl->hasSid = true;
    }

    loaded = true;
    return true;
}

//==============================================================================
juce::AudioBuffer<float> PiperEngine::synthesize (const juce::String& text, float speakingRate)
{
    juce::AudioBuffer<float> empty;

    if (! loaded || impl->session == nullptr)
        return empty;

    // length_scale > 1 = slower; invert speakingRate so >1 = faster.
    const float lengthScale = voice.lengthScale / juce::jlimit (0.25f, 4.0f, speakingRate);

    const auto sentences = Phonemizer::textToPhonemeIds (text, voice);
    if (sentences.empty())
        return empty;

    const auto mem = Ort::MemoryInfo::CreateCpu (OrtArenaAllocator, OrtMemTypeDefault);

    // ~60 ms gap between sentences so they don't run together.
    const int gapSamples = voice.sampleRate / 16;

    std::vector<float> audio;

    for (const auto& ids : sentences)
    {
        if (ids.empty())
            continue;

        std::vector<std::int64_t> phonemeIds (ids.begin(), ids.end());

        const std::array<std::int64_t, 2> inputShape { 1, (std::int64_t) phonemeIds.size() };
        std::array<std::int64_t, 1> lengths { (std::int64_t) phonemeIds.size() };
        const std::array<std::int64_t, 1> lengthsShape { 1 };
        std::array<float, 3> scales { voice.noiseScale, lengthScale, voice.noiseW };
        const std::array<std::int64_t, 1> scalesShape { 3 };
        std::array<std::int64_t, 1> sid { 0 };
        const std::array<std::int64_t, 1> sidShape { 1 };

        std::vector<Ort::Value> inputs;
        std::vector<const char*> inputNames;

        inputs.push_back (Ort::Value::CreateTensor<std::int64_t> (
            mem, phonemeIds.data(), phonemeIds.size(), inputShape.data(), inputShape.size()));
        inputNames.push_back ("input");

        inputs.push_back (Ort::Value::CreateTensor<std::int64_t> (
            mem, lengths.data(), lengths.size(), lengthsShape.data(), lengthsShape.size()));
        inputNames.push_back ("input_lengths");

        inputs.push_back (Ort::Value::CreateTensor<float> (
            mem, scales.data(), scales.size(), scalesShape.data(), scalesShape.size()));
        inputNames.push_back ("scales");

        if (impl->hasSid)
        {
            inputs.push_back (Ort::Value::CreateTensor<std::int64_t> (
                mem, sid.data(), sid.size(), sidShape.data(), sidShape.size()));
            inputNames.push_back ("sid");
        }

        const char* outputNames[] { "output" };

        try
        {
            auto outputs = impl->session->Run (Ort::RunOptions { nullptr },
                                               inputNames.data(), inputs.data(),
                                               inputs.size(), outputNames, 1);

            const float* out = outputs.front().GetTensorData<float>();
            const size_t count = outputs.front().GetTensorTypeAndShapeInfo().GetElementCount();

            audio.insert (audio.end(), out, out + count);
            audio.insert (audio.end(), (size_t) gapSamples, 0.0f);
        }
        catch (const Ort::Exception&)
        {
            return empty;
        }
    }

    if (audio.empty())
        return empty;

    juce::AudioBuffer<float> buffer (1, (int) audio.size());
    buffer.copyFrom (0, 0, audio.data(), (int) audio.size());
    return buffer;
}
