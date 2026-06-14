# bundle_fixup_win.ps1 — Windows equivalent of bundle_fixup.sh.
#
# Makes the built VST3 self-contained on Windows: the native DLLs sit next to the
# plugin binary (Windows resolves DLLs from the loading module's own directory),
# and espeak-ng-data + the voice models go under Contents/Resources where the
# engine's BundlePaths looks for them at runtime.
#
# STATUS: UNVERIFIED scaffold — written without a Windows test loop. Expect the
# first CI run to need adjustment (DLL names, export symbols, data layout).
# See WINDOWS.md.
param(
    [Parameter(Mandatory=$true)][string]$BinaryDir,    # ...\Contents\x86_64-win
    [Parameter(Mandatory=$true)][string]$ResourcesDir, # ...\Contents\Resources
    [Parameter(Mandatory=$true)][string]$OnnxDll,
    [Parameter(Mandatory=$true)][string]$EspeakDll,
    [Parameter(Mandatory=$true)][string]$EspeakData,   # the espeak-ng-data folder
    [Parameter(Mandatory=$true)][string]$VoicesDir
)
$ErrorActionPreference = "Stop"

New-Item -ItemType Directory -Force -Path $BinaryDir, $ResourcesDir | Out-Null

# 1) DLLs next to the plugin binary.
foreach ($dll in @($OnnxDll, $EspeakDll)) {
    if (-not (Test-Path $dll)) { throw "Missing DLL: $dll" }
    Copy-Item $dll -Destination $BinaryDir -Force
    Write-Host "  copied $(Split-Path $dll -Leaf) -> $BinaryDir"
}

# 2) espeak-ng-data -> Resources/espeak-ng-data
if (Test-Path $EspeakData) {
    Copy-Item $EspeakData -Destination (Join-Path $ResourcesDir "espeak-ng-data") -Recurse -Force
    Write-Host "  copied espeak-ng-data -> $ResourcesDir"
} else {
    throw "Missing espeak-ng-data: $EspeakData"
}

# 3) Voice models -> Resources/voices
if (Test-Path $VoicesDir) {
    Copy-Item $VoicesDir -Destination (Join-Path $ResourcesDir "voices") -Recurse -Force
    Write-Host "  copied voices -> $ResourcesDir"
}

Write-Host "Windows bundle fixup complete."
