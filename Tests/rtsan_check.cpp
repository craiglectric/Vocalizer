// Real-time safety harness for Vocalizer (dev tool, not shipped).
//
// An "audio thread" calls the real VocalizerAudioProcessor::processBlock continuously
// (random block sizes, output-gain drags, dense MIDI including SysEx and velocity-0
// note-offs, melody armed/disarmed), while the main thread plays the editor: it arms and
// clears the melody, runs two real Generates (worker-thread render -> lock-free handoff
// into the AuditionPlayer, so the retire queue is exercised), toggles play/stop/loop,
// reads the playhead, pumps serviceMessageThread() (buffer GC) and saves/restores state.
//
// In the VOCALIZER_RTSAN tree processBlock is [[clang::nonblocking]]: any allocation,
// lock or blocking syscall inside it aborts the run (recipe in CMakeLists.txt).

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <random>
#include <thread>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace
{
    constexpr double kFs         = 48000.0;
    constexpr int    kBlock      = 512;
    constexpr int    kMaxHostBlk = 2048;

    void pump (int ms)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
    }

    bool waitForRender (VocalizerAudioProcessor& p)
    {
        for (int waited = 0; p.isRendering() && waited < 60000; waited += 50)
        {
            pump (50);
            p.serviceMessageThread();
        }
        return ! p.isRendering();
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;   // MessageManager for the render callAsync

    std::unique_ptr<juce::AudioProcessor> base (createPluginFilter());
    auto& proc = dynamic_cast<VocalizerAudioProcessor&> (*base);

    proc.setPlayConfigDetails (0, 2, kFs, kBlock);
    proc.prepareToPlay (kFs, kBlock);

    auto* gainParam = proc.apvts.getParameter (ParamID::outputGain);

    // Pre-built MIDI traffic (built here, not on the audio thread): notes, velocity-0
    // note-offs, CCs, pitch-bend and a long SysEx (> 8 bytes, stored out of line).
    juce::MidiBuffer busyMidi;
    {
        const juce::uint8 sysex[] = { 0x43, 0x10, 0x4c, 0x00, 0x00, 0x7e, 0x00, 0x01,
                                      0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09 };
        int t = 0;
        for (int n = 0; n < 24; ++n)
        {
            busyMidi.addEvent (juce::MidiMessage::noteOn (1, 48 + n, (juce::uint8) 100), t);
            busyMidi.addEvent (juce::MidiMessage::controllerEvent (1, 1, n), t + 1);
            busyMidi.addEvent (juce::MidiMessage::noteOn (1, 48 + n, (juce::uint8) 0), t + 10);
            busyMidi.addEvent (juce::MidiMessage::pitchWheel (1, 8192 + n * 50), t + 12);
            t += 20;
        }
        busyMidi.addEvent (juce::MidiMessage::createSysExMessage (sysex, (int) sizeof (sysex)), 3);
        busyMidi.addEvent (juce::MidiMessage::noteOff (1, 60), 500);
    }

    std::atomic<bool> stop { false };
    std::atomic<long> blocks { 0 };
    std::atomic<bool> finite { true };

    std::thread audio ([&]
    {
        std::mt19937 rng (99);
        std::uniform_int_distribution<int> blk (1, kMaxHostBlk);
        std::uniform_real_distribution<float> uni (0.0f, 1.0f);
        juce::AudioBuffer<float> buf (2, kMaxHostBlk);
        juce::MidiBuffer empty;

        while (! stop.load())
        {
            const long b = blocks.load();
            const int n = (b % 5 == 0) ? blk (rng) : kBlock;
            buf.setSize (2, n, false, false, true);   // capacity pre-sized: no realloc

            if (b % 8 == 0 && gainParam != nullptr)
                gainParam->setValueNotifyingHost (uni (rng));   // host automation

            // busyMidi's event times go up to 500: only hand it over on big blocks.
            auto& midi = (b % 3 == 0 && n > 500) ? busyMidi : empty;
            proc.processBlock (buf, midi);

            for (int i = 0; i < n; ++i)
                if (! std::isfinite (buf.getSample (0, i)))
                    finite = false;

            blocks.fetch_add (1);
            std::this_thread::sleep_for (std::chrono::microseconds (500));
        }
    });

    bool ok = true;

    // Melody capture while MIDI is flowing.
    proc.setMelodyArmed (true);
    pump (300);
    proc.setMelodyArmed (false);
    std::printf ("  melody: %d notes captured\n", (int) proc.getMelodySnapshot().notes.size());

    // Two Generates -> two handoffs (the second retires the first buffer on the audio
    // thread through the lock-free retire queue), with audition running.
    for (int g = 0; g < 2; ++g)
    {
        proc.setText (g == 0 ? "Real time safety check." : "A second render replaces the first.");
        proc.generate();
        if (! waitForRender (proc))
        {
            std::printf ("  generate %d: timed out\n", g);
            ok = false;
            break;
        }
        std::printf ("  generate %d: %s\n", g, proc.getStatusMessage().toRawUTF8());

        proc.setLooping (g == 0);
        proc.play();
        for (int i = 0; i < 20; ++i)
        {
            pump (20);
            juce::ignoreUnused (proc.getPlayheadNormalized(), proc.isPlaying(), proc.hasRenderedAudio());
            proc.serviceMessageThread();
            if (i == 10) proc.togglePlay();
            if (i == 12) proc.togglePlay();
        }
    }
    if (! proc.hasRenderedAudio())
    {
        std::printf ("  no rendered audio (voices / TTS libs missing?)\n");
        ok = false;
    }

    // Re-arm + clear while running, then save / restore state (restores the melody).
    proc.setMelodyArmed (true);
    pump (200);
    {
        juce::MemoryBlock mb;
        proc.getStateInformation (mb);
        proc.setStateInformation (mb.getData(), (int) mb.getSize());
    }
    proc.clearMelody();
    proc.stop();
    pump (100);

    stop = true;
    audio.join();
    proc.serviceMessageThread();
    proc.releaseResources();

    ok = ok && finite.load();
    std::printf ("  %ld audio blocks, output %s\n", blocks.load(), finite.load() ? "finite" : "NON-FINITE");
    std::printf ("%s\n", ok ? "rtsan_check: OK" : "rtsan_check: FAILED");
    return ok ? 0 : 1;
}
