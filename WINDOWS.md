# Vocalizer on Windows (VST3)

> **Status: UNVERIFIED first cut.** macOS (arm64) is the proven, shipping build.
> The Windows pipeline below was authored without a Windows test loop, so expect
> the first CI run to need a debugging pass. It is wired into
> [`.github/workflows/windows.yml`](.github/workflows/windows.yml) and produces an
> **unsigned** installer.

## What's in place

- **CMake** (`CMakeLists.txt`) branches the native-TTS stack by platform:
  - macOS → arm64 `.dylib`s under `ThirdParty/onnxruntime-arm64` + `ThirdParty/espeak-ng-install` (unchanged).
  - Windows → `.dll`/`.lib` under `ThirdParty/onnxruntime-win-x64` + `ThirdParty/espeak-ng-install-win`.
- **Bundle fixup**: macOS uses `scripts/bundle_fixup.sh` (@loader_path dylibs);
  Windows uses `scripts/bundle_fixup_win.ps1`, which copies `onnxruntime.dll` +
  `espeak-ng.dll` next to the plugin binary (`Contents/x86_64-win`) and
  `espeak-ng-data` + the voice models into `Contents/Resources`.
- **CI** downloads the onnxruntime Windows x64 prebuilt, builds the rhasspy
  espeak-ng fork with the same options as `scripts/build_espeak_ng.sh`, builds the
  VST3, runs the fixup, and packages with Inno Setup.

## Likely first-run issues to check

1. **espeak-ng MSVC build** — confirm `espeak-ng.dll` + `espeak-ng.lib` land where
   CMake expects (`bin/` and `lib/`). MSVC may place the import lib in a `Release/`
   subdir; adjust `IMPORTED_IMPLIB` / the install step if so. Confirm the rhasspy
   fork still exports `espeak_TextToPhonemesWithTerminator` on Windows.
2. **onnxruntime version** — the engine targets 1.14.1; the macOS dylib is
   `libonnxruntime.1.14.1.dylib`. The Windows DLL is just `onnxruntime.dll`. If the
   engine hard-codes a versioned name anywhere, reconcile it.
3. **DLL discovery at load** — Windows resolves DLLs from the loading module's
   directory, so the DLLs sit beside the VST3 binary. Verify a DAW loads it with no
   missing-DLL error (Dependencies/`dumpbin /dependents`).
4. **Voice/data licensing** — same caveat as macOS: confirm each bundled voice
   model's license permits redistribution before shipping the installer.

## To finish

Push this repo, run the **windows** workflow (Actions tab → Run workflow), read the
logs, and iterate on the above. Signing is intentionally omitted — add a
`signtool` step + cert when ready (the store already lists Windows; unsigned just
means a SmartScreen prompt on the installer).
