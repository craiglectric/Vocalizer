# Vocalizer

Type text, pick a voice, and a MIDI melody drives the pitch so the voice *sings*
your words — a stylized autotuned/vocoder vocal you can drag straight onto an
audio track. JUCE 8 instrument plugin (VST3 + AU), macOS + Windows.

See [`Vocalizer_CLAUDE.md`](Vocalizer_CLAUDE.md) for the full design, scope, and
licensing notes. **Read §2 (licensing) before Phase 1** — the phonemizer choice
can force the whole plugin's license.

## Status

**Feature-complete (v1), Phases 0–6.** Type text → optionally capture a MIDI
melody → pick a voice → **Generate** → the words are sung onto your melody
(TD-PSOLA, formant-preserving) → audition through the track → **drag the WAV**
onto an audio track. VST3 + AU; full neon GUI; state (incl. melody) persists.

Native **arm64** on Apple Silicon (no Rosetta). Remaining release work (needs
your resources) is in [`RELEASE.md`](RELEASE.md): optional **Universal** (for
Intel Macs), Developer-ID **notarization**, a **Windows** build, and
by-ear/Ableton QA.

## Build

```bash
scripts/build_espeak_ng.sh                    # one-time: build espeak-ng (arm64) from source
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release          # self-contains + installs to ~/Library
```

The build bundles the TTS dylibs (onnxruntime + espeak-ng + libucd) + espeak
data + voices **inside** the .vst3/.component (relocatable via `@loader_path`,
ad-hoc signed) and installs to the user plugin folders. Optional installer:
`bash scripts/build_installer.sh`. Validate: `auval -v aumu Vclz Cowd`.

### GUI preview (no DAW needed)

```bash
cmake -B build -DVOCALIZER_BUILD_TOOLS=ON
cmake --build build --target render_gui
./build/render_gui gui_preview.png
```

## Validate (macOS)

```bash
auval -v aumu Vclz Cowd
pluginval --strictness-level 10 --validate "<built-vst3>"
```

## License

Vocalizer is **free and open source under the GNU GPLv3** — see [`LICENSE`](LICENSE).

The plugin's text-to-phoneme step uses **espeak-ng (GPLv3)**, which makes the
whole plugin GPLv3 (the licensing decision is logged in
[`Vocalizer_CLAUDE.md`](Vocalizer_CLAUDE.md) §2, option a). If Vocalizer ever
needs to ship closed-source, the phonemizer must be swapped for a permissive
one (option c) first.

### Third-party components

- **JUCE 8** — under its own license (see the JUCE license terms).
- **ONNX Runtime** (MIT), **Piper** inference + **piper-phonemize** (MIT).
- **espeak-ng** (GPLv3).
- **Voice models** (`Resources/voices/*.onnx`) are **not** committed to this repo
  (see `.gitignore`). Each Piper voice model carries its own license tied to its
  training dataset — **verify each model's license before redistributing it**
  (e.g. inside a binary installer). Download voices from the upstream Piper voices
  release, or drop your own into `~/Documents/Vocalizer/Voices/`.
