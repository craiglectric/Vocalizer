#include "Parameters.h"
#include "VoicePresets.h"

namespace Params
{
    const juce::StringArray& voiceNames()
    {
        // Discovered once at startup from the curated bundle + user voices folder
        // (CLAUDE.md §5). Cached so the Choice parameter's options stay stable for
        // the host across the session.
        static const juce::StringArray names = VoicePresets::names();
        return names;
    }

    const juce::StringArray& autotuneModeNames()
    {
        static const juce::StringArray names { "MIDI-FOLLOW", "SCALE-SNAP" };
        return names;
    }

    const juce::StringArray& keyNames()
    {
        static const juce::StringArray names
        {
            "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
        };
        return names;
    }

    const juce::StringArray& scaleNames()
    {
        static const juce::StringArray names
        {
            "CHROMATIC", "MAJOR", "MINOR", "DORIAN", "MIXOLYDIAN",
            "PENTATONIC MAJ", "PENTATONIC MIN"
        };
        return names;
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        using namespace juce;
        AudioProcessorValueTreeState::ParameterLayout layout;

        // voicePreset — Choice
        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { ParamID::voicePreset, 1 }, "Voice", voiceNames(), 0));

        // speakingRate — Float 0.5..2.0x (1.0 = native model rate)
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { ParamID::speakingRate, 1 }, "Speaking Rate",
            NormalisableRange<float> { 0.5f, 2.0f, 0.001f }, 1.0f));

        // autotuneOn — Bool
        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { ParamID::autotuneOn, 1 }, "Autotune", true));

        // autotuneMode — Choice { MIDI-follow, Scale-snap }
        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { ParamID::autotuneMode, 1 }, "Autotune Mode",
            autotuneModeNames(), 0));

        // key — Choice (12)
        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { ParamID::key, 1 }, "Key", keyNames(), 0));

        // scale — Choice
        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { ParamID::scale, 1 }, "Scale", scaleNames(), 1 /* Major */));

        // retuneSpeed — Float 0..500 ms (0 = hard snap / robotic)
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { ParamID::retuneSpeed, 1 }, "Retune Speed",
            NormalisableRange<float> { 0.0f, 500.0f, 0.1f }, 20.0f, "ms"));

        // strength — Float 0..1 (blend corrected vs original pitch)
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { ParamID::strength, 1 }, "Strength",
            NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, 1.0f));

        // formantPreserve — Float 0..1
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { ParamID::formantPreserve, 1 }, "Formant Preserve",
            NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, 1.0f));

        // outputGain — Float dB
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { ParamID::outputGain, 1 }, "Output Gain",
            NormalisableRange<float> { -24.0f, 12.0f, 0.1f }, 0.0f, "dB"));

        // clipSync — Bool. On: time-stretch the render to the captured melody /
        // MIDI-clip length so it lines up with the project (CLAUDE.md §4.3).
        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { ParamID::clipSync, 1 }, "Clip Sync", true));

        return layout;
    }
}
