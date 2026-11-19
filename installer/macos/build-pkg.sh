#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Builds the macOS installer (.pkg) from a staged release folder.
#
#   installer/macos/build-pkg.sh <stage-dir> <version> [out-dir]
#
# The installer shows the licence and lets the user choose what to install:
# the editor, the standalone apps, and each plugin format separately. It
# needs macOS (pkgbuild and productbuild come with the Xcode tools).
set -euo pipefail

stage=$(cd "${1:?staged release folder}" && pwd)
version=${2:?version}
out=${3:-dist}
here=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$out"
out=$(cd "$out" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/pkgs" "$work/res"

# One component package per choice. Bundles are marked non-relocatable, so
# Installer puts them where we say rather than "updating" a copy it finds
# elsewhere on the disk.
component() { # id source-dir install-location
    local id=$1 src=$2 dest=$3
    [ -d "$src" ] && [ -n "$(ls -A "$src" 2>/dev/null)" ] || return 1
    local plist="$work/$id.plist"
    pkgbuild --analyze --root "$src" "$plist" >/dev/null
    local i=0
    while /usr/libexec/PlistBuddy -c "Print :$i" "$plist" >/dev/null 2>&1; do
        /usr/libexec/PlistBuddy -c "Set :$i:BundleIsRelocatable false" "$plist"
        i=$((i + 1))
    done
    pkgbuild --root "$src" --component-plist "$plist" --identifier "audio.mend.$id" --version "$version" \
        --install-location "$dest" "$work/pkgs/$id.pkg" >/dev/null
    return 0
}

# the editor on its own (the staged folder also holds the plugin folders)
mkdir -p "$work/editor"
cp -R "$stage/MendEditor.app" "$work/editor/"

choices=""
add() { # id title description source install-location
    if component "$1" "$4" "$5"; then
        choices+="$1 "
        cat >>"$work/choices.xml" <<EOF
    <choice id="$1" title="$2" description="$3">
        <pkg-ref id="audio.mend.$1"/>
    </choice>
    <pkg-ref id="audio.mend.$1" version="$version" onConclusion="none">$1.pkg</pkg-ref>
EOF
    fi
}
: >"$work/choices.xml"
add editor "Mend Editor" "The editor: open a recording, repair it with any of the tools, and export." \
    "$work/editor" "/Applications/Mend"
add standalone "Standalone apps" "Each tool as its own app, for live input." "$stage/Standalone" "/Applications/Mend/Tools"
add au "Audio Unit plugins" "For Logic Pro, GarageBand and other Audio Unit hosts." "$stage/AU" \
    "/Library/Audio/Plug-Ins/Components"
add vst3 "VST3 plugins" "For most other hosts (Cubase, Ableton Live, Reaper, ...)." "$stage/VST3" \
    "/Library/Audio/Plug-Ins/VST3"
add clap "CLAP plugins" "For hosts that support CLAP (Bitwig, Reaper, ...)." "$stage/CLAP" "/Library/Audio/Plug-Ins/CLAP"
add lv2 "LV2 plugins" "For hosts that support LV2 (Ardour, Carla, ...)." "$stage/LV2" "/Library/Audio/Plug-Ins/LV2"

outline=""
for c in $choices; do outline+="        <line choice=\"$c\"/>"$'\n'; done

# Installer shows plain text in its licence and welcome panes.
cp "$stage/LICENSE.txt" "$work/res/LICENSE.txt" 2>/dev/null || cp "$stage/LICENSE" "$work/res/LICENSE.txt"
sed "s/@VERSION@/$version/g" "$here/welcome.txt" >"$work/res/welcome.txt"
[ -f "$here/background.png" ] && cp "$here/background.png" "$work/res/"

cat >"$work/distribution.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>Mend $version</title>
    <welcome file="welcome.txt"/>
    <license file="LICENSE.txt"/>
    <options customize="always" require-scripts="false" hostArchitectures="x86_64,arm64"/>
    <domains enable_localSystem="true"/>
    <choices-outline>
$outline    </choices-outline>
$(cat "$work/choices.xml")
</installer-gui-script>
EOF

pkg="$out/Mend-$version-macos.pkg"
productbuild --distribution "$work/distribution.xml" --resources "$work/res" --package-path "$work/pkgs" "$pkg" >/dev/null
echo "$pkg"
