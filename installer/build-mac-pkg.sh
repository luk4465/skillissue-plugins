#!/bin/bash
# Builds all SkillIssue plug-ins (universal arm64 + x86_64, VST3 + AU) and wraps them in one macOS installer:
#   dist/SkillIssue-Plugins-<version>.pkg   (choose plug-ins like on Windows; installs for all users)
# Needs: Xcode Command Line Tools (xcode-select --install) and CMake (brew install cmake).
# Set SKIP_BUILD=1 to package an existing build-mac folder.
set -euo pipefail
cd "$(dirname "$0")/.."

VERSION=$(sed -n 's/^#define AppVer "\(.*\)"/\1/p' installer/SkillIssue.iss | tr -d '\r')
PLUGINS=("SI Riser" "SI Reverb" "SI EQ" "SI Tune" "SI 76" "SI Saturn" "SI Echo" "SI Rack" "SI Pitch")

if [ "${SKIP_BUILD:-0}" != "1" ]; then
    targets=()
    for p in "${PLUGINS[@]}"; do t="${p// /}"; targets+=("${t}_VST3" "${t}_AU"); done
    cmake -S . -B build-mac -DCMAKE_BUILD_TYPE=Release -DSI_BUILD_TESTS=OFF
    cmake --build build-mac --config Release --parallel --target "${targets[@]}"
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p dist "$work/pkgs"

choices=""
lines=""
for p in "${PLUGINS[@]}"; do
    id="com.skillissue.$(echo "${p// /}" | tr '[:upper:]' '[:lower:]')"
    root="$work/root-${p// /}"
    mkdir -p "$root/Library/Audio/Plug-Ins/VST3" "$root/Library/Audio/Plug-Ins/Components"
    vst=$(find build-mac -type d -name "$p.vst3" -path "*Release*" | head -n1)
    au=$(find build-mac -type d -name "$p.component" -path "*Release*" | head -n1)
    [ -n "$vst" ] || { echo "missing $p.vst3"; exit 1; }
    cp -R "$vst" "$root/Library/Audio/Plug-Ins/VST3/"
    [ -n "$au" ] && cp -R "$au" "$root/Library/Audio/Plug-Ins/Components/"
    # ad-hoc signature: Apple Silicon only loads signed code
    find "$root" -maxdepth 5 \( -name "*.vst3" -o -name "*.component" \) -type d -exec codesign --force --deep -s - {} \;

    scripts="$work/scripts-${p// /}"
    mkdir -p "$scripts"
    cat > "$scripts/postinstall" <<EOF
#!/bin/bash
xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/VST3/$p.vst3" "/Library/Audio/Plug-Ins/Components/$p.component" 2>/dev/null || true
killall -9 AudioComponentRegistrar 2>/dev/null || true
exit 0
EOF
    chmod +x "$scripts/postinstall"

    pkg="${p// /}.pkg"
    pkgbuild --root "$root" --identifier "$id" --version "$VERSION" --install-location / --scripts "$scripts" "$work/pkgs/$pkg" >/dev/null
    choices+="        <line choice=\"$id\"/>"$'\n'
    lines+="    <choice id=\"$id\" title=\"$p\" start_selected=\"true\"><pkg-ref id=\"$id\"/></choice>"$'\n'
    lines+="    <pkg-ref id=\"$id\" version=\"$VERSION\" onConclusion=\"none\">$pkg</pkg-ref>"$'\n'
done

mkdir -p "$work/res"
cat > "$work/res/welcome.txt" <<EOF
SkillIssue Plugins $VERSION

Installs the SI plug-ins as VST3 and Audio Unit for all users:
  /Library/Audio/Plug-Ins/VST3
  /Library/Audio/Plug-Ins/Components

Afterwards open Ableton Live > Settings > Plug-Ins, turn on "Use VST3 Plug-in System Folders"
and "Use Audio Units", then press Rescan.
EOF

cat > "$work/distribution.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>SkillIssue Plugins $VERSION</title>
    <welcome file="welcome.txt" mime-type="text/plain"/>
    <options customize="always" require-scripts="false" hostArchitectures="arm64,x86_64"/>
    <domains enable_localSystem="true"/>
    <choices-outline>
$choices    </choices-outline>
$lines</installer-gui-script>
EOF

out="dist/SkillIssue-Plugins-$VERSION.pkg"
productbuild --distribution "$work/distribution.xml" --resources "$work/res" --package-path "$work/pkgs" "$out"
echo "Installer: $out"
