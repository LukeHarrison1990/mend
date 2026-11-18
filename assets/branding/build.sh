#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Regenerates every icon and logo file from the two scripts in this folder.
# Needs: python3 with fontTools, rsvg-convert (librsvg), ImageMagick.
set -euo pipefail
cd "$(dirname "$0")"
python3 make_icon.py
python3 make_logo.py
mkdir -p png
for s in 16 24 32; do rsvg-convert -w $s -h $s mend-icon-small.svg -o png/mend-icon-$s.png; done
for s in 48 64 128 256 512 1024; do rsvg-convert -w $s -h $s mend-icon.svg -o png/mend-icon-$s.png; done
res=../../app/resources
convert png/mend-icon-{16,24,32,48,64,128,256}.png "$res/mend.ico"
cp png/mend-icon-256.png "$res/mend.png"
python3 - "$res/mend.icns" <<'PY'
import struct, sys
body = b''
for tag, size in [('icp4', 16), ('icp5', 32), ('icp6', 64), ('ic07', 128), ('ic08', 256), ('ic09', 512), ('ic10', 1024)]:
    data = open(f'png/mend-icon-{size}.png', 'rb').read()
    body += tag.encode() + struct.pack('>I', len(data) + 8) + data
open(sys.argv[1], 'wb').write(b'icns' + struct.pack('>I', len(body) + 8) + body)
PY
python3 - ../../ui/include/mend/ui/LogoPng.hpp <<'PY'
import sys
data = open('png/mend-icon-128.png', 'rb').read()
rows = ['    ' + ', '.join(f'0x{b:02x}' for b in data[i:i + 20]) + ',' for i in range(0, len(data), 20)]
open(sys.argv[1], 'w').write(f"""// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
//
// The Mend icon (assets/branding/png/mend-icon-128.png) embedded for the
// UIs' headers. Regenerate with assets/branding/build.sh.
#pragma once

namespace mend::ui {{

static const unsigned char kLogoPng[] = {{
""" + '\n'.join(rows) + f"""
}};
static const unsigned int kLogoPngSize = {len(data)};

}} // namespace mend::ui
""")
PY
echo "Done."
