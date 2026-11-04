#!/bin/bash
# Builds all SI plug-ins (VST3 + AU, universal arm64/x86_64) and installs them for the current user (no installer).
# Needs: Xcode Command Line Tools (xcode-select --install) and CMake (brew install cmake).
set -euo pipefail
cd "$(dirname "$0")"

cmake -S . -B build-mac -DCMAKE_BUILD_TYPE=Release -DSI_BUILD_TESTS=OFF
cmake --build build-mac --config Release --parallel --target SIReverb_VST3 SIRiser_VST3 SIEQ_VST3 SITune_VST3 SI76_VST3 SISaturn_VST3 SIEcho_VST3 SIRack_VST3 SIPitch_VST3 SIReverb_AU SIRiser_AU SIEQ_AU SITune_AU SI76_AU SISaturn_AU SIEcho_AU SIRack_AU SIPitch_AU

VST3_DIR="$HOME/Library/Audio/Plug-Ins/VST3"
AU_DIR="$HOME/Library/Audio/Plug-Ins/Components"
mkdir -p "$VST3_DIR" "$AU_DIR"

for p in "SI Reverb" "SI Riser" "SI EQ" "SI Tune" "SI 76" "SI Saturn" "SI Echo" "SI Rack" "SI Pitch"; do
    target="${p// /}"
    vst=$(find build-mac -type d -name "$p.vst3" -path "*Release*" | head -n1)
    au=$(find build-mac -type d -name "$p.component" -path "*Release*" | head -n1)
    rm -rf "$VST3_DIR/$p.vst3" "$AU_DIR/$p.component"
    cp -R "$vst" "$VST3_DIR/"
    cp -R "$au" "$AU_DIR/"
    codesign --force --deep -s - "$VST3_DIR/$p.vst3"
    codesign --force --deep -s - "$AU_DIR/$p.component"
    xattr -dr com.apple.quarantine "$VST3_DIR/$p.vst3" "$AU_DIR/$p.component" 2>/dev/null || true
    echo "Installed $p ($target)"
done

# make macOS re-scan Audio Units
killall -9 AudioComponentRegistrar 2>/dev/null || true
echo "Done. Rescan plug-ins in Ableton (Settings > Plug-Ins > Rescan)."
