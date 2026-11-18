#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Collects a build into a release archive.
#
#   scripts/package.sh <build-dir> <windows|macos|linux> <version> [out-dir]
#
# Produces <out-dir>/Mend-<version>-<platform>.zip (.tar.gz on Linux) with the
# editor, the standalone apps and every plugin format built for that system.
set -euo pipefail

build=${1:?build dir}
platform=${2:?windows, macos or linux}
version=${3:?version}
out=${4:-dist}
root=$(cd "$(dirname "$0")/.." && pwd)
bin="$build/bin"
name="Mend-$version-$platform"
stage="$out/$name"

rm -rf "$stage"
mkdir -p "$stage"/{VST3,CLAP,Standalone,licenses}

copy_bundles() { # pattern destination
    local found=0
    for f in "$bin"/$1; do
        [ -e "$f" ] || continue
        cp -R "$f" "$2/"
        found=1
    done
    return 0
}

copy_bundles '*.vst3' "$stage/VST3"
copy_bundles '*.clap' "$stage/CLAP"
# LV2 bundles are only complete when their .ttl files were generated (native builds)
if ls "$bin"/*.lv2/manifest.ttl >/dev/null 2>&1; then
    mkdir -p "$stage/LV2"
    for f in "$bin"/*.lv2; do [ -f "$f/manifest.ttl" ] && cp -R "$f" "$stage/LV2/"; done
fi

case "$platform" in
windows)
    cp "$bin/MendEditor.exe" "$stage/"
    for f in "$bin"/Mend*.exe; do
        [ "$(basename "$f")" = MendEditor.exe ] || cp "$f" "$stage/Standalone/"
    done
    ;;
macos)
    mkdir -p "$stage/AU"
    copy_bundles '*.component' "$stage/AU"
    cp -R "$bin/MendEditor.app" "$stage/"
    for f in "$bin"/Mend*.app; do
        [ "$(basename "$f")" = MendEditor.app ] || cp -R "$f" "$stage/Standalone/"
    done
    ;;
linux)
    cp "$bin/MendEditor" "$stage/"
    for f in "$bin"/Mend*; do
        b=$(basename "$f")
        # standalone apps are the plain executables (no extension)
        if [ -f "$f" ] && [ -x "$f" ] && [ "$b" != MendEditor ] && [ "${b%.*}" = "$b" ]; then
            cp "$f" "$stage/Standalone/"
        fi
    done
    mkdir -p "$stage/share/applications" "$stage/share/icons"
    cp "$root/app/resources/mend.desktop" "$stage/share/applications/"
    cp "$root/assets/branding/png/mend-icon-256.png" "$stage/share/icons/mend.png"
    cp "$root/scripts/install-linux.sh" "$stage/install.sh"
    ;;
esac

# strip debug symbols where a strip tool is available (smaller downloads)
strip_tool=${STRIP:-}
if [ -z "$strip_tool" ]; then
    case "$platform" in
    windows) strip_tool=$(command -v x86_64-w64-mingw32-strip || command -v strip || true) ;;
    linux) strip_tool=$(command -v strip || true) ;;
    esac
fi
if [ -n "$strip_tool" ]; then
    find "$stage" -type f \( -name '*.exe' -o -name '*.dll' -o -name '*.so' -o -name '*.clap' -o -name '*.vst3' -o -perm -u+x \) \
        ! -name '*.sh' -exec "$strip_tool" --strip-unneeded {} \; 2>/dev/null || true
fi

cp "$root/LICENSE" "$root/THIRD_PARTY_NOTICES.md" "$stage/"
cp "$root/external/opus/COPYING" "$stage/licenses/opus.txt"
cp "$root/external/ogg/COPYING" "$stage/licenses/ogg.txt"
cp "$root/external/opusfile/COPYING" "$stage/licenses/opusfile.txt"
cp "$root/external/DPF/LICENSE" "$stage/licenses/DPF.txt"
sed "s/@VERSION@/$version/g" "$root/scripts/release-readme-$platform.txt" > "$stage/README.txt"

cd "$out"
if [ "$platform" = linux ]; then
    tar -czf "$name.tar.gz" "$name"
    echo "$out/$name.tar.gz"
else
    rm -f "$name.zip"
    if command -v zip >/dev/null; then
        zip -qry "$name.zip" "$name"
    else
        powershell -NoProfile -Command "Compress-Archive -Path '$name' -DestinationPath '$name.zip'"
    fi
    echo "$out/$name.zip"
fi
