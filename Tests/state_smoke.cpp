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

    // ---- Voice compatibility (removed voices -> CORI) ----------------------------
    {
        auto make = []
        {
            std::unique_ptr<juce::AudioProcessor> b (createPluginFilter());
            return b;
        };
        auto voiceName = [] (VocalizerAudioProcessor* p)
        {
            const int i = (int) std::lround (p->apvts.getRawParameterValue (ParamID::voicePreset)->load());
            const auto& ps = p->getVoicePresets();
            return (i >= 0 && i < ps.size()) ? ps[i].name : juce::String ("<out of range>");
        };
        // Build a session blob: the current APVTS state with voicePreset forced to
        // `index` and an optional saved voiceName (empty = a v0.1.0 session).
        auto blob = [] (VocalizerAudioProcessor* p, int index, const juce::String& name)
        {
            auto state = p->apvts.copyState();
            state.getChildWithProperty ("id", ParamID::voicePreset).setProperty ("value", index, nullptr);
            if (name.isNotEmpty()) state.setProperty ("voiceName", name, nullptr);
            juce::MemoryBlock mb;
            juce::AudioProcessor::copyXmlToBinary (*state.createXml(), mb);
            return mb;
        };

        auto base = make();
        auto* p = dynamic_cast<VocalizerAudioProcessor*> (base.get());
        check (voiceName (p) == "CORI (UK)", "default voice is CORI (UK)");

        // v0.1.0 sessions stored only an index into the old LESSAC/AMY/RYAN/...
        // table (7 = RYAN ROBOT, 0 = LESSAC) — all load as CORI (UK).
        for (int legacy : { 0, 3, 7 })
        {
            auto other = make();
            auto* q = dynamic_cast<VocalizerAudioProcessor*> (other.get());
            auto mb = blob (p, legacy, {});
            if (auto* vp = q->apvts.getParameter (ParamID::voicePreset)) vp->setValueNotifyingHost (vp->convertTo0to1 (2.0f));
            q->setStateInformation (mb.getData(), (int) mb.getSize());
            char msg[120];
            std::snprintf (msg, sizeof msg, "legacy index %d loads as CORI (UK) (got %s)", legacy, voiceName (q).toRawUTF8());
            check (voiceName (q) == "CORI (UK)", msg);
        }

        // Named sessions: removed voices map to Cori (ROBOT keeps ROBOT); a stale
        // index is overridden by the name.
        const std::pair<const char*, const char*> named[] =
        {
            { "LESSAC",       "CORI (UK)" },  { "RYAN ROBOT", "CORI ROBOT" },
            { "ALAN (UK)",    "CORI (UK)" },  { "KRISTIN (US)", "KRISTIN (US)" },
            { "JOHN (US)",    "JOHN (US)" },
        };
        for (const auto& [savedName, want] : named)
        {
            auto other = make();
            auto* q = dynamic_cast<VocalizerAudioProcessor*> (other.get());
            auto mb = blob (p, 1, savedName);
            q->setStateInformation (mb.getData(), (int) mb.getSize());
            char msg[160];
            std::snprintf (msg, sizeof msg, "saved voice '%s' loads as %s (got %s)", savedName, want, voiceName (q).toRawUTF8());
            check (voiceName (q) == want, msg);
        }

        // Round trip through get/setState keeps a non-default voice by name.
        {
            const auto& ps = p->getVoicePresets();
            int norman = -1;
            for (int i = 0; i < ps.size(); ++i) if (ps[i].name == "NORMAN (US)") norman = i;
            if (auto* vp = p->apvts.getParameter (ParamID::voicePreset)) vp->setValueNotifyingHost (vp->convertTo0to1 ((float) norman));
            juce::MemoryBlock mb; p->getStateInformation (mb);
            auto other = make();
            auto* q = dynamic_cast<VocalizerAudioProcessor*> (other.get());
            q->setStateInformation (mb.getData(), (int) mb.getSize());
            check (norman >= 0 && voiceName (q) == "NORMAN (US)", "NORMAN (US) round-trips by name");
        }
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
