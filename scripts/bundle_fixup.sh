#!/usr/bin/env bash
# Phase 6 — make a built plugin bundle self-contained, then install it.
# Copies the arm64 TTS dylibs next to the binary, espeak-ng-data + voices into
# Resources, strips detritus, ad-hoc codesigns, and installs to the plugin folder.
# All dylibs already use @rpath install names (onnxruntime prebuilt + the
# source-built espeak-ng/libucd), so the only fixup is the binary's rpath.
#
# Args: bundleDir macosDir resDir ortLib espeakLib ucdLib espeakData voicesDir installPath
set -euo pipefail

bundle="$1"; macos="$2"; res="$3"
ortLib="$4"; espeakLib="$5"; ucdLib="$6"; espeakData="$7"; voices="$8"; install="$9"

binary="$macos/Vocalizer"

# 1) Copy the dylibs next to the binary (deref symlinks; make writable).
cp -fL "$ortLib"    "$macos/libonnxruntime.1.14.1.dylib"
cp -fL "$espeakLib" "$macos/libespeak-ng.dylib"
cp -fL "$ucdLib"    "$macos/libucd.dylib"
chmod u+w "$macos"/libonnxruntime.1.14.1.dylib "$macos"/libespeak-ng.dylib "$macos"/libucd.dylib

# 2) espeak finds libucd via @rpath -> ensure it searches its own directory.
if ! otool -l "$macos/libespeak-ng.dylib" | grep -q "@loader_path"; then
    install_name_tool -add_rpath "@loader_path" "$macos/libespeak-ng.dylib"
fi

# 3) Leave only @loader_path on the plugin binary (drop dev rpaths CMake adds).
for rp in $(otool -l "$binary" | awk '/LC_RPATH/{f=1} f&&/path/{print $2; f=0}'); do
    [ "$rp" != "@loader_path" ] && install_name_tool -delete_rpath "$rp" "$binary" || true
done

# 4) Resources: espeak-ng-data + voices.
mkdir -p "$res/voices"
rm -rf "$res/espeak-ng-data"
cp -R "$espeakData" "$res/espeak-ng-data"
cp -f "$voices"/*.onnx "$voices"/*.onnx.json "$res/voices/" 2>/dev/null || true

# 5) Strip detritus, then ad-hoc sign the dylibs and the whole bundle.
find "$bundle" -name '._*' -delete
xattr -cr "$bundle"
codesign --force --sign - "$macos/libonnxruntime.1.14.1.dylib" \
                          "$macos/libespeak-ng.dylib" \
                          "$macos/libucd.dylib"
codesign --force --deep --sign - "$bundle"

# 6) Install the self-contained, signed bundle.
mkdir -p "$(dirname "$install")"
rm -rf "$install"
cp -R "$bundle" "$install"

echo "self-contained (arm64) + installed: $install"
