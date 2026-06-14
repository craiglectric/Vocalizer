# CLAUDE.md — Vocalizer

> Project context for Claude Code. Read this file at the start of every session before touching code.
> **Working name "Vocalizer"** (pairs with Computerizer as a Cowden Audio "-izer" plugin). Rename = one CMake field + the folder; do it now if you want a different name.

---

## 1. What we're building

**Vocalizer** turns typed text into a sung/spoken vocal you can drop straight onto an audio track. You type words, pick a voice preset, and a **MIDI melody on the track drives the pitch** so the voice *sings* your text. Optional autotune-style pitch correction shapes how robotic vs natural it sounds. The rendered result auditions inside the plugin and **drags out as a WAV onto any audio track**.

It loads on a **MIDI/instrument track** (it generates audio and reads MIDI for pitch). Typical flow:
1. Type text in the plugin.
2. Put a MIDI clip (your melody) on the same track; arm + capture it in the plugin.
3. Hit **Generate** → offline render of speech → pitch-retuned to follow the MIDI notes.
4. **Audition** it through the track output, then **drag the WAV** onto an audio track.

### Honest scope — what "singing" means here
This is **not** a true singing-synthesis engine (Vocaloid / Synthesizer V / DiffSinger), which need lyric-to-note alignment and are either commercial or heavyweight. Vocalizer produces the **stylized autotuned/vocoder vocal** sound: clean spoken words from neural TTS, then pitch forced onto your MIDI melody. Think robot-singer / T-Pain / talkbox vibe — musically excellent for electronic/dubstep work, but it won't sound like a natural human vocalist. The *words* advance at the TTS pace; the *pitch* follows MIDI. True syllable-locked-to-note timing is a documented stretch goal (§4.3), not v1.

### Hard requirements
- **Formats:** VST3 + AU. **Platforms:** macOS + Windows.
  - ⚠️ **AU is macOS-only** → macOS gets VST3 + AU (v2, the format Ableton hosts on Mac); Windows gets VST3 only.
- **Framework:** JUCE 8.x. **Build:** CMake. **Host target:** Ableton Live.
- **Plugin type:** instrument that also takes MIDI → `IS_SYNTH TRUE`, `NEEDS_MIDI_INPUT TRUE`, `NEEDS_MIDI_OUTPUT FALSE`. MIDI is used **only as a pitch target**, not to trigger clip playback.
- **TTS engine:** bundled neural (**Piper**, ONNX) — offline, consistent across machines, clean voiced output that pitch-corrects well.

---

## 2. ⚠️ Licensing & dependencies — READ BEFORE BUILDING

This is the load-bearing decision. **Resolve it before writing engine code**, because it can force the whole plugin's license.

| Dependency | Role | License (verify current!) | Risk |
|------------|------|---------------------------|------|
| JUCE 8 | Framework | AGPL or paid JUCE license | You already use JUCE; pick the right license tier for a commercial release. |
| ONNX Runtime | Runs the Piper model | MIT | ✅ Permissive. |
| Piper (inference code) | Neural TTS | MIT | ✅ Permissive. |
| **Phonemizer / G2P** | text → phonemes | **espeak-ng = GPLv3** | ❌ **Viral for closed-source.** This is the trap. |
| Voice models (.onnx) | The actual voices | **Per-model — varies** | ⚠️ Some trained on restricted datasets. Check each before bundling. |

**The espeak-ng problem:** Piper's C++ path phonemizes text via `piper-phonemize`, which wraps **espeak-ng (GPLv3)**. Linking GPLv3 into a closed-source plugin means you'd have to open-source the plugin. Options, decide explicitly:
- **(a) Accept GPL / open-source the plugin** — simplest technically.
- **(b) Run espeak-ng as a separate process** and treat it as aggregation — legally grey, get advice, and subprocess-spawning fights AU hardened-runtime / notarization on Mac.
- **(c) Replace the G2P with a permissive dictionary-based phonemizer** (CMUdict lookup + a small rule/neural fallback for out-of-vocabulary words) — more dev work, but keeps the plugin closed-source-able. **Recommended if this is a commercial product.**

> Treat all license rows above as "verify against current upstream before shipping," and get real legal advice before a paid release. Don't let Claude Code silently `FetchContent` espeak-ng into a closed-source build without a decision logged here.

> **DECISION (2026-06-13): Option (a) — accept GPL, the plugin is open-source (GPLv3).** This is a non-commercial project, so espeak-ng (GPLv3) is used directly for G2P via the standard Piper path. Consequence: the whole plugin must be distributed under a GPLv3-compatible license. If this ever goes commercial/closed, revisit and switch to option (c).

**Bundle-size reality:** each Piper voice is ~20–60 MB. Bundling 6–10 voices = 200–500 MB+ plus the ONNX Runtime dylib. Plan: ship a small curated set, and load extra user-supplied models from a `~/Documents/Vocalizer/Voices/` folder.

---

## 3. Build & toolchain

CMake + JUCE via `FetchContent` (as in the Computerizer project). Plus ONNX Runtime (prebuilt per-platform binaries from Microsoft, vendored or fetched) and the chosen phonemizer.

### Commands
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
cmake --build build --config Release --target Vocalizer_VST3   # single target
```

### CMake notes
- `juce_add_plugin(Vocalizer ... FORMATS VST3 AU IS_SYNTH TRUE NEEDS_MIDI_INPUT TRUE ...)`, `COMPANY_NAME "Cowden Audio"`, unique 4-char `PLUGIN_CODE` (e.g. `Vclz`), `AU_MAIN_TYPE "kAudioUnitType_MusicDevice"` (instrument).
- Link `onnxruntime` (and ship its shared lib alongside the plugin binary; **codesign + notarize the dylib too on macOS**).
- Bundle voice models + phonemizer data via `juce_add_binary_data` or as resource files copied into the bundle/installer.
- `juce::juce_audio_utils juce::juce_dsp juce::juce_gui_extra` + recommended flag targets.

### Folder structure
```
Vocalizer/
├── CLAUDE.md
├── CMakeLists.txt
├── README.md
├── .gitignore
├── Source/
│   ├── PluginProcessor.{h,cpp}
│   ├── PluginEditor.{h,cpp}
│   ├── params/
│   │   ├── Parameters.{h,cpp}        ← APVTS layout
│   │   └── VoicePresets.{h,cpp}      ← voice model table
│   ├── tts/
│   │   ├── PiperEngine.{h,cpp}       ← ONNX load + inference
│   │   ├── Phonemizer.{h,cpp}        ← G2P (SEE §2 — license-sensitive)
│   │   └── VoiceModel.{h,cpp}        ← model + config JSON loader
│   ├── dsp/
│   │   ├── PitchTracker.{h,cpp}      ← YIN/pYIN
│   │   ├── PitchCorrector.{h,cpp}    ← PSOLA / phase-vocoder retune
│   │   └── AuditionPlayer.{h,cpp}    ← buffer playback voice
│   ├── midi/
│   │   └── MelodyRecorder.{h,cpp}    ← captures MIDI pitch timeline
│   ├── render/
│   │   └── RenderJob.{h,cpp}         ← background generate pipeline
│   └── gui/
│       ├── NeonLookAndFeel.{h,cpp}   ← reuse from Computerizer
│       ├── TextInputPanel.{h,cpp}
│       ├── VoiceSelector.{h,cpp}
│       ├── AutotunePanel.{h,cpp}
│       ├── WaveformDisplay.{h,cpp}
│       └── DragHandle.{h,cpp}        ← drag-to-track export
├── Resources/
│   ├── voices/        ← bundled .onnx + .json (curated set)
│   └── fonts/
└── Tests/
```

---

## 4. Architecture

### 4.1 High-level flow
```
type text ──┐
MIDI clip ──┤ capture into MelodyRecorder (pitch timeline)
            │
   [Generate] (background thread, offline):
            ├─ Phonemizer: text → phonemes
            ├─ PiperEngine: phonemes → ONNX → speech buffer (22.05 kHz)
            ├─ resample → host sample rate
            ├─ PitchTracker: analyze speech pitch per frame
            ├─ PitchCorrector: retune frames toward MIDI/scale target
            └─ write result → audition buffer + temp WAV
            │
   audition playback (processBlock) ──► track output
   DragHandle.performExternalDragDropOfFiles(tempWav) ──► audio track
```

**Nothing heavy runs on the audio thread.** `processBlock` only (a) feeds incoming MIDI into `MelodyRecorder` and (b) plays the audition buffer if playing. Generation/retune is offline on a background thread.

### 4.2 TTS engine (PiperEngine)
Piper inference pipeline: text → **phonemes** (Phonemizer) → **phoneme IDs** (per the voice's config JSON) → **ONNX VITS model** → float audio at the model's native rate (usually 22050 Hz). Then resample to host SR.

- Load model + config once when a voice preset is selected (cache; loading is slow — do it off the audio thread).
- ONNX Runtime session is created on a worker thread; inference is heavy → **never on the audio thread**.
- Speaking rate / length-scale and pitch baseline come from the voice preset (§5).

### 4.3 MIDI-driven pitch model (the core musical feature)
- **MelodyRecorder** (audio thread, lock-free write): captures note-on/note-off with sample-accurate timestamps into a pitch timeline. UI exposes **Arm / Capture / Clear**. Practically: arm it, play the track's MIDI clip (or play notes live), and it records the melody contour.
- **Retune target:** during offline correction, for each speech analysis frame, the target pitch = the MIDI note active at the corresponding time. `retuneSpeed` controls glide between targets (0 = instant hard snap → robotic; higher = portamento). When no note is active: hold last note, or pass dry (a per-preset/param setting).
- **Length mismatch** (speech duration vs MIDI timeline): default **stretch the MIDI timeline to fit the speech length** so every word gets pitched; alternative absolute-time mode is a toggle. Document whichever ships.
- **v1 = pitch-only follow** (no time-warping of syllables). **Stretch goal:** segment syllables and time-stretch them to land on note boundaries for true note-locked singing — much harder, separate phase.
- **Scale-snap fallback:** when autotune mode = Scale-snap (or no MIDI captured), retune to nearest note in Key+Scale instead of MIDI.

### 4.4 Autotune / pitch correction (PitchCorrector) — offline, so we can do it well
Because correction is offline, use a quality formant-preserving algorithm (PSOLA for voice, or phase-vocoder with formant correction) with look-ahead — no real-time constraints.
- **PitchTracker:** YIN/pYIN per frame to get the speech's source pitch.
- **PitchCorrector:** shift each frame so source → target pitch, formant-preserved (no chipmunk). `strength` blends corrected vs original pitch; `retuneSpeed`/glide sets snap hardness.

### 4.5 Audition playback (AuditionPlayer)
A simple sample-playback voice that reads the rendered buffer and mixes to output in `processBlock`. Atomic play position; Play/Stop (and optional loop + transport-follow) from the UI. Fully real-time safe (buffer read only).

### 4.6 Drag-to-track export (DragHandle)
- On Generate, also write the final (retuned, host-SR) buffer to a temp WAV via `juce::WavAudioFormat` (44.1/48 kHz, 24-bit).
- Editor is a `juce::DragAndDropContainer`; the drag handle calls `performExternalDragDropOfFiles({tempWav}, /*canMoveFiles*/ false)`. Ableton imports it as a clip.
- **Temp-file lifecycle:** unique temp dir per instance; keep the file alive across the drag (DAW copies on import); clean up on close. Don't delete mid-drag.

### 4.7 Threading & handoff
- **RenderJob** runs on a `juce::ThreadPool` / dedicated `juce::Thread`: phonemize → infer → resample → track → correct → write WAV. Cancellable; reports progress.
- Completion posts to the message thread (`MessageManager::callAsync` / `AsyncUpdater`). New audio buffer handed to the audio thread via **atomic pointer swap** (lock-free); old buffer freed on the message thread.
- ONNX session + heavy DSP: worker thread only.

### 4.8 State (APVTS + extras)
Persist: text, selected voice preset, all autotune params, and the captured MIDI melody (pitch timeline). Optionally embed the last rendered WAV in state behind a flag (can be large) so reopening a session restores the clip; otherwise restore params + melody and require a re-Generate.

---

## 5. Voice / style presets
A preset = **a bundled Piper voice model + speaking params + optional autotune defaults**. "Style" here is voice-model + rate/pitch + downstream autotune character (Piper itself isn't very expressive). Be honest about that in copy.

Curate ~6 distinct voices (e.g. male / female / neutral / a deliberately lo-fi "robotic" one / an accent or two), each with default `speakingRate`, pitch baseline, and a sensible autotune default (e.g. the "robot" voice ships with hard-snap on). Plus a **user models folder** (`~/Documents/Vocalizer/Voices/`) scanned at startup so users can add their own Piper models without a plugin update. Verify each bundled model's license (§2).

---

## 6. GUI — neon cyberpunk (reuse Computerizer's `NeonLookAndFeel`)
Same visual language as Computerizer / CrateID: near-black base, neon cyan/magenta accents, glow, mono/techno type.
- **Text input** — multiline neon field, "terminal" feel.
- **Voice selector** — neon list/dropdown of presets with the active one glowing; small name readout.
- **Autotune panel** — On/Off, Mode (MIDI-follow / Scale-snap), Key, Scale, Retune Speed, Strength, Formant Preserve. Glowing knobs/toggles, attached to APVTS.
- **MIDI melody strip** — Arm / Capture / Clear + a small contour readout of the captured notes.
- **Generate button** + progress/spinner (background render can take seconds; never blocks UI).
- **Waveform display** of the rendered clip.
- **Audition transport** — Play/Stop (+ optional loop).
- **Drag handle** — clear "drag to track" affordance (e.g. a glowing waveform chip you can grab).
- Default size ≈ 560 × 640 (more controls than Computerizer); constrained-ratio resizable.

GUI never calls audio-thread code except via atomics / async. All painting in `paint()`.

---

## 7. Parameters (APVTS)
Automatable params (text + captured melody are state, not params):
- `voicePreset` — Choice
- `speakingRate` — Float (0.5–2.0×)
- `autotuneOn` — Bool
- `autotuneMode` — Choice {MIDI-follow, Scale-snap}
- `key` — Choice (12)
- `scale` — Choice {Chromatic, Major, Minor, …}
- `retuneSpeed` — Float (0–500 ms; 0 = hard snap)
- `strength` — Float (0–1)
- `formantPreserve` — Float (0–1) or Bool
- `outputGain` — Float (dB)

---

## 8. Build plan / milestones
Keep a loadable plugin in Ableton at the end of every phase.

- **Phase 0 — Skeleton.** Repo, CMake, JUCE FetchContent. Empty instrument (`IS_SYNTH`, `NEEDS_MIDI_INPUT`) builds VST3 + AU, loads on a MIDI track, passes silence.
- **Phase 1 — Decide & wire deps.** **Resolve §2 licensing first.** Get ONNX Runtime linking + a single Piper voice doing text→audio on a worker thread, written to a temp WAV. Prove the toolchain (incl. Mac notarization of the dylib).
- **Phase 2 — Generate + audition.** Background RenderJob, progress UI, atomic buffer handoff, AuditionPlayer playback through the track. Waveform display.
- **Phase 3 — Drag-to-track.** `DragAndDropContainer` + `performExternalDragDropOfFiles`, temp-file lifecycle, verified import into Ableton on both OSes.
- **Phase 4 — MIDI melody + retune.** MelodyRecorder capture, PitchTracker, PitchCorrector, MIDI-follow + scale-snap modes. Tune the autotune params by ear.
- **Phase 5 — Voices + presets + GUI.** Curate voices, VoicePresets table, user-models folder, full neon UI + AutotunePanel + autotune param attachments, state save/load incl. melody.
- **Phase 6 — Validation & release.** pluginval (strictness 10), `auval` on Mac, QA in Ableton both OSes, installer that ships ONNX Runtime + voices, codesign/notarize everything.

---

## 9. Conventions & guardrails for Claude Code

**Real-time safety in `processBlock` — non-negotiable:**
- Only MIDI capture (lock-free write) + audition buffer read. **No TTS, no ONNX, no pitch correction, no allocation, no locks, no file I/O** on the audio thread.
- `juce::ScopedNoDenormals`; size everything in `prepareToPlay`.
- Hand new buffers to the audio thread via atomic pointer swap; free old buffers on the message thread.

**Offline render rules:**
- All generation on a worker thread; cancellable; progress reported async.
- ONNX session creation + inference are worker-thread only and cached per voice.
- Resample TTS output (≈22.05 kHz) to host SR before correction/export.

**Licensing discipline:** never add espeak-ng (or any GPL dep) to the build without a logged decision in §2. Prefer the permissive G2P path for a commercial release.

**General:** C++20, JUCE 8 idioms, `juce::dsp` where it fits. Sample-rate/block-size independent. Support mono + stereo out. Manage temp files carefully (unique dir, alive during drag, cleaned on close). Verify dependency + voice-model licenses before shipping.

**How to add a voice:** drop `.onnx` + config `.json` into `Resources/voices/` (or the user folder), add a row to the `VoicePresets` table with default rate/pitch/autotune — no other code change.

**Validation:**
```bash
pluginval --strictness-level 10 --validate "<built-vst3>"
auval -v aumu Vclz Cowd     # macOS instrument
```

**Definition of done (per phase):** builds clean both OSes (no new warnings), loads + runs in Ableton without dropouts, pluginval passes, and Generate → audition → drag-to-track works end to end.

---

## 10. Open decisions to revisit
- **§2 phonemizer/license path** — the #1 blocker; decide before Phase 1.
- Embed rendered audio in plugin state, or restore params + re-Generate? (size vs convenience)
- Length-mismatch handling: stretch MIDI-to-speech (default) vs absolute-time.
- How many voices to bundle vs leave to the user folder (size budget).
- Syllable-to-note time-locking = future phase, not v1.
- AUv3 / Standalone targets deferred unless needed.
