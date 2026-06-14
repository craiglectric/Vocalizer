#pragma once

#include <juce_core/juce_core.h>

//==============================================================================
// VoicePresets — the curated voice table (CLAUDE.md §5). A preset = a bundled
// Piper model + speaking params + autotune defaults. Several presets may share a
// model (e.g. a soft "LESSAC" and a hard-snap "LESSAC ROBOT"). User-supplied
// models dropped into ~/Documents/Vocalizer/Voices/ are discovered at startup
// and appended, so users can add voices without a plugin update.
//==============================================================================
struct VoicePreset
{
    juce::String name;          // display name (also the APVTS Choice entry)
    juce::File   jsonFile;      // "<model>.onnx.json"

    // Per-preset defaults applied when the preset is selected in the UI.
    float defaultSpeakingRate    = 1.0f;
    bool  defaultAutotuneOn      = true;
    int   defaultAutotuneMode    = 0;      // 0 = MIDI-follow, 1 = Scale-snap
    float defaultRetuneSpeedMs   = 20.0f;
    float defaultStrength        = 1.0f;
    float defaultFormantPreserve = 1.0f;
};

class VoicePresets
{
public:
    static juce::File bundledVoicesDir();
    static juce::File userVoicesDir();      // ~/Documents/Vocalizer/Voices

    // Curated bundled presets (only those whose model file exists) followed by
    // any user-folder models. Never empty if at least one model is present.
    static juce::Array<VoicePreset> discover();

    // Display names for the voicePreset Choice parameter.
    static juce::StringArray names();
};
