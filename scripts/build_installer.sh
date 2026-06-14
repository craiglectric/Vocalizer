#!/usr/bin/env bash
# Phase 6 — build a macOS .pkg installer that places the VST3 + AU into the
# system plugin folders. Run after a release build (and, for distribution, after
# codesign_notarize.sh so the bundles are Developer-ID signed + stapled).
#
# Usage:
#   scripts/build_installer.sh [version] ["Developer ID Installer: Name (TEAMID)"]
# If the installer-signing identity is omitted the .pkg is unsigned (fine for
# local use; distribution should sign + notarize the .pkg too).
set -euo pipefail

VERSION="${1:-0.1.0}"
INSTALLER_ID="${2:-}"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
REL="$ROOT/build/Vocalizer_artefacts/Release"
STAGE="$ROOT/build/installer_root"
OUT="$ROOT/build/Vocalizer-${VERSION}.pkg"

rm -rf "$STAGE"
mkdir -p "$STAGE/Library/Audio/Plug-Ins/VST3" \
         "$STAGE/Library/Audio/Plug-Ins/Components"

# COPYFILE_DISABLE keeps cp from writing AppleDouble (._*) files that would trip
# pkg notarization.
export COPYFILE_DISABLE=1
cp -R "$REL/VST3/Vocalizer.vst3"      "$STAGE/Library/Audio/Plug-Ins/VST3/"
cp -R "$REL/AU/Vocalizer.component"   "$STAGE/Library/Audio/Plug-Ins/Components/"
xattr -cr "$STAGE"
/usr/bin/find "$STAGE" -name '._*' -delete

PKGARGS=(--root "$STAGE" --install-location "/"
         --identifier com.cowdenaudio.vocalizer
         --version "$VERSION")
[ -n "$INSTALLER_ID" ] && PKGARGS+=(--sign "$INSTALLER_ID")

pkgbuild "${PKGARGS[@]}" "$OUT"
echo "built $OUT"
[ -n "$INSTALLER_ID" ] && echo "Notarize the pkg: xcrun notarytool submit \"$OUT\" --keychain-profile <PROFILE> --wait && xcrun stapler staple \"$OUT\""
