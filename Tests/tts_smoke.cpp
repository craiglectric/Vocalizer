// Phase 1 toolchain proof: load a Piper voice, synthesize text -> WAV, off any
// audio thread. Proves espeak-ng + piper-phonemize + ONNX Runtime + the voice
// model all link and produce audio.
//
//   cmake -B build -DVOCALIZER_BUILD_TOOLS=ON
//   cmake --build build --target tts_smoke
//   ./build/tts_smoke "Some text to speak." out.wav
//
// Voice + espeak data default to the dev paths baked in by CMake.

#include "tts/PiperEngine.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cstdio>

#ifndef VOCALIZER_DEFAULT_VOICE_JSON
 #define VOCALIZER_DEFAULT_VOICE_JSON ""
#endif
#ifndef ESPEAK_DATA_DIR
 #define ESPEAK_DATA_DIR ""
#endif

int main (int argc, char** argv)
{
    // Pure console tool: File I/O + WAV writing need no MessageManager.
    const juce::String text = (argc > 1)
        ? juce::String::fromUTF8 (argv[1])
        : "Hello from Vocalizer. Your typed words, sung by a robot.";

    const juce::File voiceJson { VOCALIZER_DEFAULT_VOICE_JSON };
    const juce::File espeakDir { ESPEAK_DATA_DIR };

    std::printf ("voice : %s\n", voiceJson.getFullPathName().toRawUTF8());
    std::printf ("espeak: %s\n", espeakDir.getFullPathName().toRawUTF8());

    PiperEngine engine;
    if (! engine.loadVoice (voiceJson, espeakDir))
    {
        std::fprintf (stderr, "FAIL: could not load voice / init espeak / open ONNX model.\n");
        return 1;
    }
    std::printf ("loaded voice, sample rate %d Hz\n", engine.getSampleRate());

    auto buffer = engine.synthesize (text);
    if (buffer.getNumSamples() == 0)
    {
        std::fprintf (stderr, "FAIL: synthesis produced no audio.\n");
        return 2;
    }

    const int    sr   = engine.getSampleRate();
    const int    n    = buffer.getNumSamples();
    const float  peak = buffer.getMagnitude (0, 0, n);
    const double secs = (double) n / (double) sr;
    std::printf ("synthesized %d samples (%.2f s), peak %.3f\n", n, secs, peak);

    // Peak-normalise a copy to -1 dBFS so the smoke WAV is comfortably audible.
    // (The engine returns RAW audio; later phases own gain staging.)
    juce::AudioBuffer<float> outBuf;
    outBuf.makeCopyOf (buffer);
    if (peak > 1.0e-6f)
        outBuf.applyGain (juce::Decibels::decibelsToGain (-1.0f) / peak);

    const juce::File out = (argc > 2)
        ? juce::File (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]))
        : juce::File::getCurrentWorkingDirectory().getChildFile ("tts_out.wav");
    out.deleteFile();

    juce::WavAudioFormat wav;
    if (auto os = std::unique_ptr<juce::FileOutputStream> (out.createOutputStream()))
    {
        std::unique_ptr<juce::AudioFormatWriter> writer (
            wav.createWriterFor (os.get(), (double) sr, 1, 16, {}, 0));
        if (writer != nullptr)
        {
            os.release();   // writer owns the stream now
            writer->writeFromAudioSampleBuffer (outBuf, 0, outBuf.getNumSamples());
            writer.reset(); // flush
            std::printf ("wrote %s\n", out.getFullPathName().toRawUTF8());
            return 0;
        }
    }

    std::fprintf (stderr, "FAIL: could not write WAV.\n");
    return 3;
}
