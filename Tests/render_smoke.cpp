// Phase 2 integration proof: drive the REAL plugin through a full Generate, then
// audition it, all headless — no DAW. Proves the worker-thread RenderJob, the
// async completion + lock-free buffer handoff, and AuditionPlayer playback in
// processBlock actually produce audio.
//
//   cmake -B build -DVOCALIZER_BUILD_TOOLS=ON
//   cmake --build build --target render_smoke
//   ./build/render_smoke

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <cstdio>
#include <vector>
#include <algorithm>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

#include "PluginProcessor.h"
#include "dsp/PitchTracker.h"
#include "dsp/PitchCorrector.h"

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;   // MessageManager for callAsync

    const double sr = 48000.0;
    const int    block = 512;

    std::unique_ptr<juce::AudioProcessor> base (createPluginFilter());
    auto* proc = dynamic_cast<VocalizerAudioProcessor*> (base.get());
    if (proc == nullptr) { std::fprintf (stderr, "FAIL: wrong processor type\n"); return 1; }

    proc->setPlayConfigDetails (0, 2, sr, block);
    proc->prepareToPlay (sr, block);

    proc->setText ("Testing one two three. Vocalizer renders in the plugin.");

    // --- Phase 4: capture a 2-note melody through the real processBlock --------
    // A3 (57, 220 Hz) for the first ~0.8 s, then E4 (64, ~330 Hz).
    proc->setMelodyArmed (true);
    {
        juce::AudioBuffer<float> b (2, block);
        const double captureSecs = 1.6;
        const int    nBlocks = (int) (captureSecs * sr / block);
        const int    switchBlock = nBlocks / 2;
        for (int bi = 0; bi < nBlocks; ++bi)
        {
            juce::MidiBuffer midi;
            if (bi == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, 57, (juce::uint8) 100), 0);
            else if (bi == switchBlock)
            {
                midi.addEvent (juce::MidiMessage::noteOff (1, 57), 0);
                midi.addEvent (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 100), 1);
            }
            else if (bi == nBlocks - 1)
                midi.addEvent (juce::MidiMessage::noteOff (1, 64), 0);

            b.clear();
            proc->processBlock (b, midi);
        }
    }
    proc->setMelodyArmed (false);
    const double melodyDur = proc->getMelodySnapshot().durationSec;
    std::printf ("captured melody: %d notes, %.2f s\n",
                 (int) proc->getMelodySnapshot().notes.size(), melodyDur);

    std::printf ("generating…\n");
    proc->generate();

    // Pump the message loop until the render completes (or time out).
    const int timeoutMs = 30000;
    int waited = 0;
    while (proc->isRendering() && waited < timeoutMs)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        waited += 50;
    }
    if (proc->isRendering()) { std::fprintf (stderr, "FAIL: render timed out\n"); return 2; }

    std::printf ("status: %s\n", proc->getStatusMessage().toRawUTF8());
    if (! proc->hasRenderedAudio()) { std::fprintf (stderr, "FAIL: no rendered audio\n"); return 3; }

    // Audition: play and run processBlock, measuring output level.
    proc->play();

    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    float peak = 0.0f;
    int silentBlocks = 0, totalBlocks = 0;

    // ~3 seconds of audio.
    const int blocksToRun = (int) (3.0 * sr / block);
    for (int b = 0; b < blocksToRun; ++b)
    {
        buf.clear();
        proc->processBlock (buf, midi);
        const float m = buf.getMagnitude (0, block);
        peak = juce::jmax (peak, m);
        if (m < 1.0e-5f) ++silentBlocks;
        ++totalBlocks;
    }

    std::printf ("auditioned %d blocks, peak %.3f, %d silent\n", totalBlocks, peak, silentBlocks);

    if (peak < 1.0e-3f) { std::fprintf (stderr, "FAIL: audition output silent\n"); return 4; }

    // Phase 3: the drag-to-track source WAV must exist and be a valid, readable
    // clip (this is exactly what gets dropped onto a DAW track).
    const juce::File wav = proc->getRenderedWavFile();
    if (! wav.existsAsFile()) { std::fprintf (stderr, "FAIL: temp WAV missing\n"); return 5; }

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (wav));
    if (reader == nullptr) { std::fprintf (stderr, "FAIL: temp WAV not readable\n"); return 6; }

    std::printf ("drag WAV: %s — %.0f Hz, %d-bit, %lld samples (%.2f s), %lld bytes\n",
                 wav.getFileName().toRawUTF8(), reader->sampleRate,
                 (int) reader->bitsPerSample, (long long) reader->lengthInSamples,
                 (double) reader->lengthInSamples / reader->sampleRate,
                 (long long) wav.getSize());

    if (reader->lengthInSamples < (juce::int64) (0.5 * reader->sampleRate))
    { std::fprintf (stderr, "FAIL: temp WAV implausibly short\n"); return 7; }

    // Clip-sync (default ON): rendered length should match the captured melody
    // length so it lines up with the MIDI clip.
    const double wavDur = (double) reader->lengthInSamples / reader->sampleRate;
    std::printf ("clip-sync: render %.2f s vs melody %.2f s\n", wavDur, melodyDur);
    if (std::abs (wavDur - melodyDur) > 0.15)
    { std::fprintf (stderr, "FAIL: clip-sync length mismatch\n"); return 9; }

    // --- Phase 4: confirm the rendered pitch FOLLOWS the captured melody --------
    // First third should sit near A3 (220 Hz), last third near E4 (~330 Hz).
    const int wavN = (int) reader->lengthInSamples;
    juce::AudioBuffer<float> clip (1, wavN);
    reader->read (&clip, 0, wavN, 0, true, false);

    auto contour = PitchTracker::analyze (clip.getReadPointer (0), wavN, reader->sampleRate);
    std::vector<float> firstThird, lastThird;
    for (size_t f = 0; f < contour.f0.size(); ++f)
    {
        const float v = contour.f0[f];
        if (v <= 0.0f) continue;
        const double t = (contour.firstCenter + (double) f * contour.hop) / reader->sampleRate;
        const double total = (double) wavN / reader->sampleRate;
        if (t < total / 3.0)        firstThird.push_back (v);
        else if (t > 2.0 * total / 3.0) lastThird.push_back (v);
    }

    auto median = [] (std::vector<float> v) -> float
    {
        if (v.empty()) return 0.0f;
        std::sort (v.begin(), v.end());
        return v[v.size() / 2];
    };

    const float f0a = median (firstThird);
    const float f0b = median (lastThird);
    const float wantA = PitchCorrector::midiToHz (57.0f);   // 220 Hz
    const float wantB = PitchCorrector::midiToHz (64.0f);   // ~330 Hz
    std::printf ("melody-follow: first third %.1f Hz (want %.1f), last third %.1f Hz (want %.1f)\n",
                 f0a, wantA, f0b, wantB);

    const bool followsA = f0a > 0.0f && std::abs (f0a - wantA) / wantA < 0.10f;
    const bool followsB = f0b > 0.0f && std::abs (f0b - wantB) / wantB < 0.10f;
    if (! (followsA && followsB))
    { std::fprintf (stderr, "FAIL: rendered pitch did not follow the MIDI melody\n"); return 8; }

    // Re-generate: the drag source must point at a NEW unique file (so DAWs
    // can't reuse a previously-imported clip with the same path).
    const juce::String firstName = proc->getRenderedWavFile().getFileName();
    proc->setText ("A second, different render.");
    proc->generate();
    for (int w = 0; proc->isRendering() && w < timeoutMs; w += 50)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    const juce::String secondName = proc->getRenderedWavFile().getFileName();
    std::printf ("re-generate: drag file %s -> %s\n", firstName.toRawUTF8(), secondName.toRawUTF8());
    if (firstName == secondName || ! proc->getRenderedWavFile().existsAsFile())
    { std::fprintf (stderr, "FAIL: re-generate did not produce a fresh unique drag file\n"); return 10; }

    std::printf ("PASS: render + audition + unique drag WAV + MIDI-melody pitch-follow.\n");
    return 0;
}
