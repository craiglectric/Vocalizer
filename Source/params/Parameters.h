#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

//==============================================================================
// Parameter definitions for Vocalizer (CLAUDE.md §7).
//
// These are the AUTOMATABLE params only. The typed text and the captured MIDI
// melody (pitch timeline) are persisted as plugin STATE, not as parameters
// (see PluginProcessor get/setStateInformation), because they aren't sensible
// host-automation targets.
//==============================================================================
namespace ParamID
{
    inline constexpr auto voicePreset    = "voicePreset";
    inline constexpr auto speakingRate   = "speakingRate";
    inline constexpr auto autotuneOn     = "autotuneOn";
    inline constexpr auto autotuneMode   = "autotuneMode";
    inline constexpr auto key            = "key";
    inline constexpr auto scale          = "scale";
    inline constexpr auto retuneSpeed    = "retuneSpeed";
    inline constexpr auto strength       = "strength";
    inline constexpr auto formantPreserve = "formantPreserve";
    inline constexpr auto outputGain     = "outputGain";
    inline constexpr auto clipSync       = "clipSync";
}

namespace Params
{
    // Voice preset names (VoicePresets::names(): curated bundled voices, CORI
    // first, then any user-folder models).
    const juce::StringArray& voiceNames();

    // Autotune retune-target mode (CLAUDE.md §4.3 / §7).
    const juce::StringArray& autotuneModeNames();   // { MIDI-follow, Scale-snap }

    // 12 pitch classes for the Scale-snap key.
    const juce::StringArray& keyNames();

    // Scale shapes for Scale-snap fallback.
    const juce::StringArray& scaleNames();

    // Builds the full APVTS parameter layout.
    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
}
