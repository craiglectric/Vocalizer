// How-to video renderer: real editor + real audio, offline (../shared/howto/HowToHost.h).
// Vocalizer's workflow is UI actions, so the timeline drives them through "call" events:
//   { "call": "prerender", "text", "voice", "notes": [[t, dur, note], ...], "length" }  (t = 0: a take
//        captured and generated before the video starts, then auditioned on LOOP)
//   { "call": "voice", "name": "CORI (UK)" }   pick a voice (loads its defaults, like the VOICE menu)
//   { "call": "text", "value": "..." }          what's in the text box (type it with several events)
//   { "call": "arm", "on": true }   { "call": "clear" }   { "call": "generate" }
//   { "call": "play" }  (waits for a render in flight)   { "call": "stop" }   { "call": "loop", "on": true }
#include "../../../shared/howto/HowToHost.h"
#include "PluginProcessor.h"

namespace
{
template <typename T>
T* findChild (juce::Component* root, std::function<bool (T&)> match)
{
    if (root == nullptr) return nullptr;
    for (auto* c : root->getChildren())
    {
        if (auto* t = dynamic_cast<T*> (c))
            if (match (*t)) return t;
        if (auto* deeper = findChild<T> (c, match)) return deeper;
    }
    return nullptr;
}

juce::TextButton* button (juce::Component* ed, const juce::String& prefix)
{
    return findChild<juce::TextButton> (ed, [&] (juce::TextButton& b) { return b.getButtonText().startsWith (prefix); });
}

void waitForRender (VocalizerAudioProcessor& v)
{
    for (int waited = 0; v.isRendering() && waited < 120000; waited += 20)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
}

void selectVoice (VocalizerAudioProcessor& v, const juce::String& name)
{
    const auto& presets = v.getVoicePresets();
    for (int i = 0; i < presets.size(); ++i)
        if (presets[i].name.equalsIgnoreCase (name))
        {
            if (auto* p = v.apvts.getParameter (ParamID::voicePreset))
                p->setValueNotifyingHost (p->convertTo0to1 ((float) i));
            v.applyVoiceDefaults (i);
            return;
        }
    std::printf ("warning: voice not found: %s\n", name.toRawUTF8());
}
}

int main (int argc, char** argv)
{
    return howto::run (argc, argv, [] (juce::AudioProcessor& p, juce::AudioProcessorEditor* ed, const juce::var& e)
    {
        auto& v = dynamic_cast<VocalizerAudioProcessor&> (p);
        const auto call = e["call"].toString();
        auto click = [&] (const juce::String& prefix)
        {
            if (auto* b = button (ed, prefix)) { if (b->onClick) b->onClick(); }
            else std::printf ("warning: no %s button\n", prefix.toRawUTF8());
        };
        auto toggle = [&] (const juce::String& prefix, bool on)
        {
            if (auto* b = button (ed, prefix)) { b->setToggleState (on, juce::dontSendNotification); if (b->onClick) b->onClick(); }
            else std::printf ("warning: no %s button\n", prefix.toRawUTF8());
        };

        if (call == "prerender")
        {
            selectVoice (v, e["voice"].toString());
            v.setText (e["text"].toString());
            v.clearMelody();
            v.setMelodyArmed (true);
            const double sr = p.getSampleRate();
            const int block = 256;
            juce::MidiMessageSequence seq;
            if (auto* arr = e["notes"].getArray())
                for (auto& n : *arr)
                {
                    seq.addEvent (juce::MidiMessage::noteOn (1, (int) n[2], (juce::uint8) 100), (double) n[0] * sr);
                    seq.addEvent (juce::MidiMessage::noteOff (1, (int) n[2]), ((double) n[0] + (double) n[1]) * sr);
                }
            seq.sort();
            const auto total = (juce::int64) ((double) e.getProperty ("length", 4.0) * sr);
            juce::AudioBuffer<float> buf (juce::jmax (2, p.getTotalNumOutputChannels()), block);
            int idx = 0;
            for (juce::int64 pos = 0; pos < total; pos += block)
            {
                juce::MidiBuffer midi;
                while (idx < seq.getNumEvents() && seq.getEventPointer (idx)->message.getTimeStamp() < (double) (pos + block))
                {
                    auto& m = seq.getEventPointer (idx++)->message;
                    midi.addEvent (m, juce::jmax (0, (int) (m.getTimeStamp() - (double) pos)));
                }
                buf.clear();
                p.processBlock (buf, midi);
            }
            v.setMelodyArmed (false);
            v.generate();
            waitForRender (v);
            v.setLooping ((bool) e.getProperty ("loop", true));
            if ((bool) e.getProperty ("play", true)) v.play();
        }
        else if (call == "voice") selectVoice (v, e["name"].toString());
        else if (call == "text")
        {
            v.setText (e["value"].toString());
            if (auto* t = findChild<juce::TextEditor> (ed, [] (juce::TextEditor&) { return true; }))
                t->setText (e["value"].toString(), false);
        }
        else if (call == "arm") toggle ("ARM", (bool) e["on"]);
        else if (call == "clear") click ("CLEAR");
        else if (call == "generate") { waitForRender (v); click ("GENERATE"); }
        else if (call == "play") { waitForRender (v); v.play(); }
        else if (call == "stop") v.stop();
        else if (call == "loop") toggle ("LOOP", (bool) e["on"]);
        else std::printf ("warning: unknown call %s\n", call.toRawUTF8());
    });
}
