#!/usr/bin/env bash
# Phase 6 — Developer ID codesign + notarize the built Vocalizer bundles.
# Run this AFTER a release build (the bundles must already be self-contained;
# the normal build self-contains + ad-hoc signs them — this re-signs with your
# Developer ID, hardened runtime + secure timestamp, then notarizes + staples).
#
# Prereqs (one-time):
#   - "Developer ID Application: <Name> (<TEAMID>)" cert in your login keychain.
#   - A stored notarytool credential profile:
#       xcrun notarytool store-credentials VOCALIZER_NOTARY \
#         --apple-id you@example.com --team-id TEAMID --password <app-specific-pw>
#
# Usage:
#   scripts/codesign_notarize.sh "Developer ID Application: Name (TEAMID)" VOCALIZER_NOTARY
set -euo pipefail

DEV_ID="${1:?Pass the Developer ID Application identity as arg 1}"
PROFILE="${2:?Pass the notarytool keychain profile name as arg 2}"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
REL="$ROOT/build/Vocalizer_artefacts/Release"

sign_bundle() {
    local bundle="$1"
    [ -d "$bundle" ] || { echo "skip (missing): $bundle"; return; }
    echo "==> signing $bundle"

    xattr -cr "$bundle"

    # Inner dylibs first (hardened runtime + timestamp).
    for dylib in "$bundle/Contents/MacOS/"*.dylib; do
        [ -e "$dylib" ] || continue
        codesign --force --options runtime --timestamp --sign "$DEV_ID" "$dylib"
    done

    # Then the bundle itself.
    codesign --force --options runtime --timestamp --deep --sign "$DEV_ID" "$bundle"
    codesign --verify --deep --strict --verbose=2 "$bundle"
}

notarize_bundle() {
    local bundle="$1"
    [ -d "$bundle" ] || return
    local zip="${bundle}.zip"
    echo "==> notarizing $bundle"
    /usr/bin/ditto -c -k --keepParent "$bundle" "$zip"
    xcrun notarytool submit "$zip" --keychain-profile "$PROFILE" --wait
    xcrun stapler staple "$bundle"
    rm -f "$zip"
    echo "    stapled OK"
}

for b in "$REL/VST3/Vocalizer.vst3" "$REL/AU/Vocalizer.component"; do
    sign_bundle "$b"
done
for b in "$REL/VST3/Vocalizer.vst3" "$REL/AU/Vocalizer.component"; do
    notarize_bundle "$b"
done

echo "Done. Re-install the signed/stapled bundles (scripts/build_installer.sh or copy them)."
