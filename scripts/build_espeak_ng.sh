#!/usr/bin/env bash
# Build the rhasspy espeak-ng fork (arm64) the way piper-phonemize expects:
# no audio backends (pcaudio/sonic), and it exports espeak_TextToPhonemesWith-
# Terminator which the vendored piper source needs. Produces
# ThirdParty/espeak-ng-install/{include,lib,share} (libespeak-ng.dylib +
# libucd.dylib, both @rpath, + espeak-ng-data). Re-run if you wipe ThirdParty.
#
# Usage: scripts/build_espeak_ng.sh [arch]   (arch defaults to arm64)
set -euo pipefail

ARCH="${1:-arm64}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TP="$ROOT/ThirdParty"
SRC="$TP/espeak-ng-src"
INSTALL="$TP/espeak-ng-install"

if [ ! -f "$SRC/CMakeLists.txt" ]; then
    echo "Fetching rhasspy/espeak-ng..."
    curl -sL --max-time 180 -o "$TP/espeak-src.zip" \
        "https://github.com/rhasspy/espeak-ng/archive/refs/heads/master.zip"
    ( cd "$TP" && unzip -q espeak-src.zip && rm -f espeak-src.zip && mv espeak-ng-master espeak-ng-src )
fi

cmake -S "$SRC" -B "$SRC/build" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="$ARCH" \
    -DBUILD_SHARED_LIBS=ON \
    -DUSE_ASYNC=OFF -DUSE_MBROLA=OFF -DUSE_LIBSONIC=OFF -DUSE_LIBPCAUDIO=OFF \
    -DUSE_KLATT=OFF -DUSE_SPEECHPLAYER=OFF -DEXTRA_cmn=ON -DEXTRA_ru=ON \
    -DCMAKE_INSTALL_PREFIX="$INSTALL"
cmake --build "$SRC/build" --target install -j4

echo "espeak-ng ($ARCH) installed to $INSTALL"
