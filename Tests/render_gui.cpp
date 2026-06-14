// Offline GUI snapshot tool: instantiates the real plugin editor and renders it
// to a PNG so the neon layout can be eyeballed without a DAW.
//
// With no args it snapshots the idle editor. Pass a second arg "generate" to run
// a real Generate first (pumping the message loop) so the waveform populates.
//
//   cmake -B build -DVOCALIZER_BUILD_TOOLS=ON
//   cmake --build build --target render_gui
//   ./build/render_gui gui_preview.png generate

#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI guiInit;

    std::unique_ptr<juce::AudioProcessor> proc (createPluginFilter());
    proc->prepareToPlay (48000.0, 512);

    juce::AudioProcessorEditor* ed = proc->createEditorAndMakeActive();  // owned by proc
    if (ed == nullptr) { std::fprintf (stderr, "No editor.\n"); return 1; }

    const bool doGenerate = (argc > 2 && juce::String (argv[2]) == "generate");
    if (doGenerate)
    {
        if (auto* v = dynamic_cast<VocalizerAudioProcessor*> (proc.get()))
        {
            v->generate();
            int waited = 0;
            while (v->isRendering() && waited < 30000)
            {
                juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
                waited += 50;
            }
            // Let the editor's 30 Hz timer pick up the new thumbnail + repaint.
            juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
        }
    }

    const auto b = ed->getLocalBounds();
    const juce::Image img = ed->createComponentSnapshot (b);

    const juce::File out = (argc > 1)
        ? juce::File (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]))
        : juce::File::getCurrentWorkingDirectory().getChildFile ("gui_preview.png");
    out.deleteFile();

    if (auto os = std::unique_ptr<juce::FileOutputStream> (out.createOutputStream()))
    {
        juce::PNGImageFormat png;
        png.writeImageToStream (img, *os);
        std::printf ("Wrote %s (%d x %d)\n", out.getFullPathName().toRawUTF8(),
                     b.getWidth(), b.getHeight());
    }

    proc->editorBeingDeleted (ed);
    delete ed;
    proc = nullptr;
    return 0;
}
