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

    // Only voices whose training data is public domain ship (licences checked
    // against the upstream Piper MODEL_CARDs; credits in Resources/voices/
    // VOICES.txt). CORI is first, so it is the default (param default = 0).
    const Curated curated[] =
    {
        { "CORI (UK)",    "en_GB-cori-medium.onnx.json",       1.0f, true, 0,  20.0f, 1.0f, 1.0f },
        { "CORI ROBOT",   "en_GB-cori-medium.onnx.json",       1.0f, true, 0,   0.0f, 1.0f, 1.0f },
        { "LJ (US)",      "en_US-ljspeech-medium.onnx.json",   1.0f, true, 0,  20.0f, 1.0f, 1.0f },
        { "KRISTIN (US)", "en_US-kristin-medium.onnx.json",    1.0f, true, 0,  20.0f, 1.0f, 1.0f },
        { "NORMAN (US)",  "en_US-norman-medium.onnx.json",     1.0f, true, 0,  20.0f, 1.0f, 1.0f },
        { "JOHN (US)",    "en_US-john-medium.onnx.json",       1.0f, true, 0,  20.0f, 1.0f, 1.0f },
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

int VoicePresets::indexForSavedName (const juce::Array<VoicePreset>& presets,
                                     const juce::String& savedName)
{
    for (int i = 0; i < presets.size(); ++i)
        if (presets[i].name == savedName)
            return i;

    // Removed voices (and unknown/user voices no longer present) fall back to
    // Cori; "ROBOT" variants keep their hard-snap character.
    const bool robot = savedName.containsIgnoreCase ("ROBOT");
    for (int i = 0; i < presets.size(); ++i)
        if (presets[i].name == (robot ? "CORI ROBOT" : defaultVoiceName))
            return i;

    for (int i = 0; i < presets.size(); ++i)
        if (presets[i].name == defaultVoiceName)
            return i;

    return 0;
}
