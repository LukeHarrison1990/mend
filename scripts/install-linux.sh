#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Installs Mend for the current user: plugins into ~/.vst3, ~/.clap and
# ~/.lv2, the apps into ~/.local/bin and a menu entry for the editor.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
mkdir -p ~/.vst3 ~/.clap ~/.lv2 ~/.local/bin ~/.local/share/applications ~/.local/share/icons/hicolor/256x256/apps
cp -R "$here"/VST3/* ~/.vst3/
cp -R "$here"/CLAP/* ~/.clap/
[ -d "$here/LV2" ] && cp -R "$here"/LV2/* ~/.lv2/
cp "$here/MendEditor" "$here"/Standalone/* ~/.local/bin/
cp "$here/share/applications/mend.desktop" ~/.local/share/applications/
cp "$here/share/icons/mend.png" ~/.local/share/icons/hicolor/256x256/apps/mend.png
echo "Installed. Make sure ~/.local/bin is on your PATH; Mend Editor is in your applications menu."
