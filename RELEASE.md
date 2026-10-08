# Vocalizer — Release / Packaging (Phase 6)

Status of CLAUDE.md §8 Phase 6 ("Validation & release").

## Done & verified

- **Native arm64.** The plugin is built **arm64** (no Rosetta). Dependencies:
  official **onnxruntime** arm64 prebuilt; **espeak-ng** (rhasspy fork) built
  from source by `scripts/build_espeak_ng.sh` with piper's options (no
  pcaudio/sonic; exports `…WithTerminator`); **piper-phonemize** compiled from
  its two source files directly into the plugin (`Source/tts/pp_*.cpp`).
- **Self-contained bundles.** The release build copies the three dylibs
  (`libonnxruntime`, `libespeak-ng`, `libucd`) into `Contents/MacOS/` and
  `espeak-ng-data` + the voice models into `Contents/Resources/`. The binary
  resolves the dylibs via `@loader_path` **only** (dev rpaths stripped), and the
  engine locates espeak data + voices inside the bundle at runtime
  (`Source/util/BundlePaths`). Relocatable — runs with the dev tree deleted.
  Verified: **native `auval` PASS** (no Rosetta), `@loader_path`-only rpath;
  `pluginval --strictness-level 10` → **SUCCESS**; all test tools pass arm64.
- **Ad-hoc signing** of the embedded dylibs + bundle happens automatically in
  the build (`scripts/bundle_fixup.sh`), so it loads locally without quarantine
  prompts.
- **Installer.** `scripts/build_installer.sh [version]` produces
  `build/Vocalizer-<version>.pkg` installing both formats to
  `/Library/Audio/Plug-Ins/{VST3,Components}`.

## Build a release (arm64, from a clean tree)

```bash
scripts/build_espeak_ng.sh                      # one-time: builds ThirdParty/espeak-ng-install
# (onnxruntime arm64 + piper-phonemize headers are under ThirdParty/ already)
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release            # self-contains + installs to ~/Library
bash scripts/build_installer.sh 0.1.0           # optional .pkg
```

## Remaining — needs your resources / decisions

1. **Universal (arm64 + x86_64), optional.** The build is now native **arm64**.
   A Universal binary additionally needs x86_64 builds of the three libs
   (onnxruntime ships universal2/x86_64; build espeak-ng x86_64 via
   `scripts/build_espeak_ng.sh x86_64` into a separate prefix and `lipo` the
   dylibs; compile the plugin with `CMAKE_OSX_ARCHITECTURES="arm64;x86_64"`).
   Only needed to support Intel Macs.

2. **Developer ID signing + notarization** (needs your Apple Developer account):
   ```bash
   # one-time:
   xcrun notarytool store-credentials VOCALIZER_NOTARY \
     --apple-id you@example.com --team-id TEAMID --password <app-specific-pw>
   # after a release build:
   scripts/codesign_notarize.sh "Developer ID Application: Name (TEAMID)" VOCALIZER_NOTARY
   scripts/build_installer.sh 0.1.0 "Developer ID Installer: Name (TEAMID)"
   xcrun notarytool submit build/Vocalizer-0.1.0.pkg --keychain-profile VOCALIZER_NOTARY --wait
   xcrun stapler staple build/Vocalizer-0.1.0.pkg
   ```

3. **Windows (VST3 only).** Needs a Windows toolchain + **Windows** builds of
   the three native libs (`onnxruntime` Windows x64 prebuilt; `espeak-ng` +
   `piper-phonemize` from source or the `piper-phonemize_windows_amd64.zip`
   release asset). The CMake is cross-platform; `BundlePaths` already handles the
   VST3-on-Windows Resources layout. Then QA in Ableton on Windows.

4. **Manual QA only you can do:** load in Ableton, capture a melody, Generate,
   audition, and **drag the clip onto a track** (the external drag gesture can't
   be tested headless); confirm it sounds right by ear on both OSes.

5. **More voices (optional).** Five public-domain voices / six presets ship
   now (credits + licences in `Resources/voices/VOICES.txt`). Before adding a
   voice, read its upstream MODEL_CARD: only redistribute models whose training
   dataset allows it (public domain / permissive). Then drop the
   `*.onnx`+`*.onnx.json` into `Resources/voices/`, add a row to the curated
   table in `Source/params/VoicePresets.cpp` **and** to `VOICES.txt` (or use
   the `~/Documents/Vocalizer/Voices/` user folder — no rebuild needed).

## GPL note

Vocalizer links espeak-ng (GPLv3), so the plugin is distributed under **GPLv3**
(CLAUDE.md §2, decision option (a)). Ship the source alongside binaries.
