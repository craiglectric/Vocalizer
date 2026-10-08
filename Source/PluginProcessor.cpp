#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "render/RenderJob.h"
#include "gui/WaveformDisplay.h"
#include "util/BundlePaths.h"

#ifndef ESPEAK_DATA_DIR
 #define ESPEAK_DATA_DIR ""
#endif
#ifndef VOCALIZER_DEFAULT_VOICE_JSON
 #define VOCALIZER_DEFAULT_VOICE_JSON ""
#endif

namespace
{
    constexpr int kThumbnailColumns = 600;
}

//==============================================================================
VocalizerAudioProcessor::VocalizerAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", Params::createLayout()),
      engine (std::make_unique<PiperEngine>())
{
    outputGainParam = apvts.getRawParameterValue (ParamID::outputGain);

    // Discover voices and point the engine at the currently-selected preset.
    presets = VoicePresets::discover();
    const int initIdx = (int) std::lround (apvts.getRawParameterValue (ParamID::voicePreset)->load());
    if (initIdx >= 0 && initIdx < presets.size())
        desiredVoiceJson = presets[initIdx].jsonFile;
    else if (! presets.isEmpty())
        desiredVoiceJson = presets[0].jsonFile;

    text = "Hello from Vocalizer. Your typed words, sung by a robot.";

    tempDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                  .getChildFile ("Vocalizer_" + juce::Uuid().toString());
    tempDir.createDirectory();
}

VocalizerAudioProcessor::~VocalizerAudioProcessor()
{
    renderPool.removeAllJobs (true, 4000);   // cancel + wait
    audition.releaseResources();
    tempDir.deleteRecursively();
}

//==============================================================================
void VocalizerAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    audition.prepare (sampleRate, samplesPerBlock);
    melody.prepare (sampleRate);
}

void VocalizerAudioProcessor::releaseResources()
{
}

bool VocalizerAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::disabled())
        return false;

    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono()
        || out == juce::AudioChannelSet::stereo();
}

void VocalizerAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                            juce::MidiBuffer& midiMessages) VOCALIZER_NONBLOCKING
{
    juce::ScopedNoDenormals noDenormals;

    // Capture the MIDI melody contour (lock-free; no-op unless armed).
    melody.processBlock (midiMessages, buffer.getNumSamples());

    buffer.clear();

    audition.process (buffer);

    const float gainDb = outputGainParam != nullptr ? outputGainParam->load() : 0.0f;
    buffer.applyGain (juce::Decibels::decibelsToGain (gainDb));
}

//==============================================================================
bool VocalizerAudioProcessor::ensureEngineReady()
{
    const juce::ScopedLock sl (engineLock);

    juce::File want = desiredVoiceJson;
    if (! want.existsAsFile())
        want = juce::File (VOCALIZER_DEFAULT_VOICE_JSON);   // dev fallback

    // Already loaded the requested voice? Done. Otherwise (different voice or
    // first run) load it on this worker thread.
    if (engine->isLoaded() && engine->getVoice().configFile == want)
        return true;

    // Prefer espeak-ng-data bundled in the plugin; fall back to the dev tree.
    juce::File espeakDir = BundlePaths::bundledEspeakDir();
    if (espeakDir == juce::File())
        espeakDir = juce::File (ESPEAK_DATA_DIR);

    return engine->loadVoice (want, espeakDir);
}

juce::String VocalizerAudioProcessor::getLoadedVoiceFileName() const
{
    const juce::ScopedLock sl (engineLock);
    return engine->isLoaded() ? engine->getVoice().configFile.getFileName() : juce::String();
}

void VocalizerAudioProcessor::applyVoiceDefaults (int presetIndex)
{
    if (presetIndex < 0 || presetIndex >= presets.size())
        return;

    const auto& vp = presets[presetIndex];

    auto setF = [this] (const char* id, float value)
    {
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    };

    setF (ParamID::speakingRate,    vp.defaultSpeakingRate);
    setF (ParamID::retuneSpeed,     vp.defaultRetuneSpeedMs);
    setF (ParamID::strength,        vp.defaultStrength);
    setF (ParamID::formantPreserve, vp.defaultFormantPreserve);
    setF (ParamID::autotuneOn,      vp.defaultAutotuneOn ? 1.0f : 0.0f);
    setF (ParamID::autotuneMode,    (float) vp.defaultAutotuneMode);
}

void VocalizerAudioProcessor::generate()
{
    if (rendering.exchange (true))
        return;   // a render is already in flight

    progress.store (0.0f);
    {
        const juce::ScopedLock sl (statusLock);
        statusMessage = "rendering…";
    }

    juce::String textToRender = getText();

    // Point the engine at the selected voice (reloaded on the worker if changed).
    {
        const int idx = (int) std::lround (apvts.getRawParameterValue (ParamID::voicePreset)->load());
        const juce::ScopedLock sl (engineLock);
        if (idx >= 0 && idx < presets.size())
            desiredVoiceJson = presets[idx].jsonFile;
    }

    // Resolve retune config from the APVTS + captured melody (message thread).
    auto getF = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };

    RenderJob::RetuneConfig rc;
    rc.autotuneOn   = getF (ParamID::autotuneOn) > 0.5f;
    rc.speakingRate = getF (ParamID::speakingRate);
    rc.pitch.mode           = (int) std::lround (getF (ParamID::autotuneMode));
    rc.pitch.key            = (int) std::lround (getF (ParamID::key));
    rc.pitch.scale          = (int) std::lround (getF (ParamID::scale));
    rc.pitch.retuneSpeedMs  = getF (ParamID::retuneSpeed);
    rc.pitch.strength       = getF (ParamID::strength);
    rc.pitch.formantPreserve = getF (ParamID::formantPreserve);
    rc.pitch.clipSync       = getF (ParamID::clipSync) > 0.5f;
    rc.melody       = melody.snapshot();

    // Unique output file per render so the DAW never reuses a cached clip of the
    // same path. Only published as the drag source once the render completes.
    const juce::File outFile = tempDir.getChildFile ("render_" + juce::String (++renderCounter) + ".wav");

    RenderJob::Callbacks cb;
    cb.ensureEngineReady = [this] { return ensureEngineReady(); };
    cb.onProgress        = [this] (float p) { progress.store (p); };
    cb.onComplete        = [this, outFile] (RenderedAudio::Ptr r)
    {
        lastRenderedWav = outFile;
        onRenderComplete (std::move (r));
    };
    cb.onFailed          = [this] (juce::String e) { onRenderFailed (e); };

    renderPool.addJob (new RenderJob (*engine, textToRender, currentSampleRate, outFile,
                                      std::move (rc), cb),
                       true /* pool owns + deletes */);
}

void VocalizerAudioProcessor::onRenderComplete (RenderedAudio::Ptr result)
{
    // Build the waveform thumbnail (message thread) before handing audio over.
    if (result != nullptr && result->buffer.getNumSamples() > 0)
    {
        thumbnail = WaveformDisplay::buildThumbnail (result->buffer.getReadPointer (0),
                                                     result->buffer.getNumSamples(),
                                                     kThumbnailColumns);
        audition.setAudio (result);
        renderGeneration.fetch_add (1);

        const double secs = result->buffer.getNumSamples() / juce::jmax (1.0, result->sampleRate);
        const juce::ScopedLock sl (statusLock);
        statusMessage = juce::String (secs, 2) + " s rendered";
    }

    progress.store (1.0f);
    rendering.store (false);
}

void VocalizerAudioProcessor::onRenderFailed (const juce::String& error)
{
    {
        const juce::ScopedLock sl (statusLock);
        statusMessage = "error: " + error;
    }
    progress.store (0.0f);
    rendering.store (false);
}

//==============================================================================
void VocalizerAudioProcessor::setText (const juce::String& newText)
{
    text = newText;
}

juce::String VocalizerAudioProcessor::getText() const
{
    return text;
}

juce::String VocalizerAudioProcessor::getStatusMessage() const
{
    const juce::ScopedLock sl (statusLock);
    return statusMessage;
}

//==============================================================================
juce::AudioProcessorEditor* VocalizerAudioProcessor::createEditor()
{
    return new VocalizerAudioProcessorEditor (*this);
}

//==============================================================================
void VocalizerAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (! state.isValid())
        return;

    // Stash the typed text + the captured MIDI melody alongside the params.
    state.setProperty ("text", text, nullptr);

    // Save the voice by NAME too: the voicePreset index depends on which models
    // are installed (bundled + user folder), so the name is what we restore by.
    {
        const int idx = (int) std::lround (apvts.getRawParameterValue (ParamID::voicePreset)->load());
        if (idx >= 0 && idx < presets.size())
            state.setProperty ("voiceName", presets[idx].name, nullptr);
    }

    const Melody m = melody.snapshot();
    juce::ValueTree melodyTree ("MELODY");
    melodyTree.setProperty ("durationSec", m.durationSec, nullptr);
    for (const auto& n : m.notes)
    {
        juce::ValueTree note ("N");
        note.setProperty ("s", n.startSec, nullptr);
        note.setProperty ("e", n.endSec, nullptr);
        note.setProperty ("n", n.note, nullptr);
        melodyTree.appendChild (note, nullptr);
    }
    state.removeChild (state.getChildWithName ("MELODY"), nullptr);
    state.appendChild (melodyTree, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void VocalizerAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            if (tree.hasProperty ("text"))
                text = tree.getProperty ("text").toString();

            if (auto melodyTree = tree.getChildWithName ("MELODY"); melodyTree.isValid())
            {
                Melody m;
                m.durationSec = (double) melodyTree.getProperty ("durationSec", 0.0);
                for (int i = 0; i < melodyTree.getNumChildren(); ++i)
                {
                    auto note = melodyTree.getChild (i);
                    m.notes.push_back ({ (double) note.getProperty ("s", 0.0),
                                         (double) note.getProperty ("e", 0.0),
                                         (int)    note.getProperty ("n", -1) });
                }
                melody.setMelody (m);
            }

            // Resolve the voice. New sessions carry "voiceName"; v0.1.0 sessions
            // only stored an index into the old table (LESSAC, AMY, RYAN, ...),
            // whose voices were removed for licensing — those load as CORI (UK).
            // The autotune params (incl. a ROBOT preset's hard snap) are saved
            // separately, so the character carries over.
            {
                const auto savedName = tree.getProperty ("voiceName").toString();
                const int idx = VoicePresets::indexForSavedName (presets, savedName);

                auto param = tree.getChildWithProperty ("id", ParamID::voicePreset);
                if (! param.isValid())
                {
                    param = juce::ValueTree ("PARAM");
                    param.setProperty ("id", ParamID::voicePreset, nullptr);
                    tree.appendChild (param, nullptr);
                }
                param.setProperty ("value", idx, nullptr);
                tree.removeProperty ("voiceName", nullptr);
            }

            apvts.replaceState (tree);
        }
    }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VocalizerAudioProcessor();
}
