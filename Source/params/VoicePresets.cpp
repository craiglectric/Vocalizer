#include "VoicePresets.h"
#include "../util/BundlePaths.h"

#ifndef VOCALIZER_VOICES_DIR
 #define VOCALIZER_VOICES_DIR ""
#endif

juce::File VoicePresets::bundledVoicesDir()
{
    // Prefer voices shipped inside the plugin bundle; fall back to the dev tree.
    if (auto inBundle = BundlePaths::bundledVoicesDir(); inBundle != juce::File())
        return inBundle;
    return juce::File (VOCALIZER_VOICES_DIR);
}

juce::File VoicePresets::userVoicesDir()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
               .getChildFile ("Vocalizer").getChildFile ("Voices");
}

juce::Array<VoicePreset> VoicePresets::discover()
{
    juce::Array<VoicePreset> out;
    const auto dir = bundledVoicesDir();

    // Curated bundled presets. Several may share one model with different
    // autotune characters (CLAUDE.md §5).
    struct Curated
    {
        const char* name;
        const char* file;
        float rate; bool atOn; int mode; float retune; float strength; float formant;
    };

    const Curated curated[] =
    {
        { "LESSAC",       "en_US-lessac-medium.onnx.json",     1.0f, true, 0,  20.0f, 1.0f, 1.0f },
        { "LESSAC ROBOT", "en_US-lessac-medium.onnx.json",     1.0f, true, 0,   0.0f, 1.0f, 1.0f },
        { "AMY",          "en_US-amy-medium.onnx.json",        1.0f, true, 0,  20.0f, 1.0f, 1.0f },
        { "RYAN",         "en_US-ryan-medium.onnx.json",       1.0f, true, 0,  20.0f, 1.0f, 1.0f },
        { "HFC FEMALE",   "en_US-hfc_female-medium.onnx.json", 1.0f, true, 0,  20.0f, 1.0f, 1.0f },
        { "ALAN (UK)",    "en_GB-alan-medium.onnx.json",       1.0f, true, 0,  20.0f, 1.0f, 1.0f },
        { "CORI (UK)",    "en_GB-cori-medium.onnx.json",       1.0f, true, 0,  20.0f, 1.0f, 1.0f },
        { "RYAN ROBOT",   "en_US-ryan-medium.onnx.json",       1.0f, true, 0,   0.0f, 1.0f, 1.0f },
    };

    for (const auto& c : curated)
    {
        const auto f = dir.getChildFile (c.file);
        if (f.existsAsFile())
            out.add ({ c.name, f, c.rate, c.atOn, c.mode, c.retune, c.strength, c.formant });
    }

    // User-supplied models (skip those already referenced by a curated preset).
    if (userVoicesDir().isDirectory())
    {
        for (const auto& entry : juce::RangedDirectoryIterator (userVoicesDir(), false, "*.onnx.json"))
        {
            const auto f = entry.getFile();
            bool already = false;
            for (const auto& p : out)
                if (p.jsonFile.getFileName() == f.getFileName())
                    { already = true; break; }

            if (! already)
            {
                VoicePreset vp;
                vp.name = f.getFileName().dropLastCharacters (10).toUpperCase(); // ".onnx.json"
                vp.jsonFile = f;
                out.add (vp);
            }
        }
    }

    return out;
}

juce::StringArray VoicePresets::names()
{
    juce::StringArray n;
    for (const auto& p : discover())
        n.add (p.name);
    if (n.isEmpty())
        n.add ("(no voices)");
    return n;
}
