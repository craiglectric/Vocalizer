// Phase 5 verification: plugin state round-trips (text + params + captured
// melody), and selecting a different voice preset actually loads a different
// model on the next render.
//
//   cmake -B build -DVOCALIZER_BUILD_TOOLS=ON
//   cmake --build build --target state_smoke && ./build/state_smoke

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include <cstdio>
#include <cmath>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

static int fails = 0;
static void check (bool ok, const char* what)
{
    std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what);
    if (! ok) ++fails;
}

static void captureMelody (VocalizerAudioProcessor* proc, double sr, int block)
{
    proc->setMelodyArmed (true);
    juce::AudioBuffer<float> b (2, block);
    const int n = (int) (1.2 * sr / block);
    for (int bi = 0; bi < n; ++bi)
    {
        juce::MidiBuffer midi;
        if (bi == 0)            midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        else if (bi == n / 2)   { midi.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
                                  midi.addEvent (juce::MidiMessage::noteOn (1, 67, (juce::uint8) 100), 1); }
        else if (bi == n - 1)   midi.addEvent (juce::MidiMessage::noteOff (1, 67), 0);
        b.clear();
        proc->processBlock (b, midi);
    }
    proc->setMelodyArmed (false);
}

static void waitRender (VocalizerAudioProcessor* proc)
{
    int waited = 0;
    while (proc->isRendering() && waited < 30000)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        waited += 50;
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const double sr = 48000.0; const int block = 512;

    // ---- State round-trip ------------------------------------------------------
    juce::MemoryBlock saved;
    Melody savedMelody;
    {
        std::unique_ptr<juce::AudioProcessor> base (createPluginFilter());
        auto* p = dynamic_cast<VocalizerAudioProcessor*> (base.get());
        p->setPlayConfigDetails (0, 2, sr, block);
        p->prepareToPlay (sr, block);

        p->setText ("State round trip test.");
        if (auto* s = p->apvts.getParameter (ParamID::strength)) s->setValueNotifyingHost (0.42f);
        if (auto* k = p->apvts.getParameter (ParamID::key))      k->setValueNotifyingHost (k->convertTo0to1 (3.0f));
        captureMelody (p, sr, block);
        savedMelody = p->getMelodySnapshot();

        p->getStateInformation (saved);
        check (savedMelody.notes.size() == 2, "captured 2-note melody before save");
    }

    {
        std::unique_ptr<juce::AudioProcessor> base (createPluginFilter());
        auto* p = dynamic_cast<VocalizerAudioProcessor*> (base.get());
        p->setPlayConfigDetails (0, 2, sr, block);
        p->prepareToPlay (sr, block);
        p->setStateInformation (saved.getData(), (int) saved.getSize());

        check (p->getText() == "State round trip test.", "text restored");

        const float strength = p->apvts.getRawParameterValue (ParamID::strength)->load();
        check (std::abs (strength - 0.42f) < 0.01f, "strength param restored");

        const int key = (int) std::lround (p->apvts.getRawParameterValue (ParamID::key)->load());
        check (key == 3, "key param restored");

        const Melody m = p->getMelodySnapshot();
        bool melodyOk = m.notes.size() == savedMelody.notes.size();
        if (melodyOk)
            for (size_t i = 0; i < m.notes.size(); ++i)
                melodyOk &= (m.notes[i].note == savedMelody.notes[i].note)
                         && std::abs (m.notes[i].startSec - savedMelody.notes[i].startSec) < 0.02;
        char msg[120];
        std::snprintf (msg, sizeof msg, "melody restored (%d notes)", (int) m.notes.size());
        check (melodyOk, msg);
    }

    // ---- Voice switching -------------------------------------------------------
    {
        std::unique_ptr<juce::AudioProcessor> base (createPluginFilter());
        auto* p = dynamic_cast<VocalizerAudioProcessor*> (base.get());
        p->setPlayConfigDetails (0, 2, sr, block);
        p->prepareToPlay (sr, block);
        p->setText ("Voice switch test.");

        const auto& presets = p->getVoicePresets();
        std::printf ("discovered %d voice presets:\n", presets.size());
        for (const auto& vp : presets)
            std::printf ("   - %-14s %s\n", vp.name.toRawUTF8(), vp.jsonFile.getFileName().toRawUTF8());
        check (presets.size() >= 2, "at least 2 voice presets discovered");

        // Render with each distinct model and confirm the loaded model matches.
        juce::StringArray seenModels;
        for (int i = 0; i < presets.size(); ++i)
        {
            if (auto* vp = p->apvts.getParameter (ParamID::voicePreset))
                vp->setValueNotifyingHost (vp->convertTo0to1 ((float) i));
            p->generate();
            waitRender (p);

            const juce::String loaded = p->getLoadedVoiceFileName();
            const juce::String want   = presets[i].jsonFile.getFileName();
            const bool audio = p->hasRenderedAudio();
            char msg[200];
            std::snprintf (msg, sizeof msg, "preset '%s' -> %s, audio=%s",
                           presets[i].name.toRawUTF8(), loaded.toRawUTF8(), audio ? "yes" : "NO");
            check (loaded == want && audio, msg);
            seenModels.addIfNotAlreadyThere (loaded);
        }
        check (seenModels.size() >= 2, "engine actually loaded >= 2 distinct models");
    }

    std::printf ("\n%s (%d failure%s)\n", fails == 0 ? "ALL PASS" : "FAILURES", fails, fails == 1 ? "" : "s");
    return fails == 0 ? 0 : 1;
}
