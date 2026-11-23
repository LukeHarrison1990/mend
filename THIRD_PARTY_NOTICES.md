# Third-party notices

Mend itself is MIT licensed (see `LICENSE`). Binaries also contain code
from the projects below, all under permissive licences compatible with MIT.
Their notices must ship alongside any binary release.

| Component | Where | Licence |
|---|---|---|
| DISTRHO Plugin Framework (DPF), (C) 2012-2025 Filipe Coelho | `external/DPF` | ISC |
| pugl (windowing, bundled with DPF), (C) David Robillard | `external/DPF/dgl/src/pugl-upstream` | ISC |
| NanoVG (bundled with DPF), (C) Mikko Mononen | `external/DPF/dgl/src/nanovg` | zlib |
| DejaVu Sans font (bundled with DPF) | DPF shared resources | Bitstream Vera / public domain |
| RtAudio / RtMidi (standalone apps, fallback audio), (C) Gary P. Scavone | bundled with DPF | MIT-style |
| CLAP headers, (C) 2014-2022 Alexandre Bique | bundled with DPF | MIT |
| LV2 headers, (C) Steve Harris, David Robillard et al. | bundled with DPF | ISC |
| miniaudio v0.11.26 (editor: file decoding, WAV writing, playback; standalone apps: audio devices), David Reid | `external/miniaudio` | Public domain or MIT-0 |
| DeepFilterNet3 model weights, (C) Hendrik Schröter and contributors (De-noise Voice mode) | `core/src/DeepFilterNet3Weights.cpp`, from https://github.com/Rikorose/DeepFilterNet | MIT or Apache-2.0 |
| stb_vorbis v1.24 (editor: Ogg Vorbis decoding), Sean Barrett | `external/stb_vorbis` | Public domain or MIT |
| libopus 1.5.2, (C) Xiph.Org Foundation, Skype Limited, Octasic, Jean-Marc Valin, Timothy B. Terriberry, CSIRO, Gregory Maxwell, Mark Borgerding, Erik de Castro Lopo, Mozilla, Amazon | `external/opus` | BSD-3-Clause (see `external/opus/COPYING`) |
| libogg 1.3.6, (C) Xiph.Org Foundation | `external/ogg` | BSD-3-Clause (see `external/ogg/COPYING`) |
| opusfile 0.12, (C) Xiph.Org Foundation and contributors | `external/opusfile` | BSD-3-Clause (see `external/opusfile/COPYING`) |

Local changes to DPF (each marked "Mend patch" in the source; to be offered
upstream):
- `distrho/src/DistrhoPluginCLAP.cpp`, `DistrhoPluginVST3.cpp`: plugin state
  loads correctly when the host supplies it in small chunks.
- `DistrhoPluginVST3.cpp`: parameter changes sent with a zero-length
  `process()` call (the VST3 way to flush parameters while stopped) are
  applied instead of dropped.
- `DistrhoPluginVST3.cpp`: in single-object mode, host edits via
  `setParamNormalized` reach the DSP (they were ignored). Together these fix
  settings being lost or reverted in Audacity 4.

The editor decodes AAC, ALAC and WMA with the operating system's own
decoders (Media Foundation on Windows, Audio Toolbox on macOS) or, on
Linux, with FFmpeg if it is installed; no code from those is included.
`external/ogg/include/ogg/config_types.h` was written for Mend in place of
the one libogg's build normally generates.

`dgl/src/pugl-upstream/src/win.c` (Mend patch): windows use the executable's own
icon resource when it has one, instead of the generic application icon.

`dgl/src/pugl.mm` (Mend patch): a one-line file including `pugl.cpp` instead
of a symbolic link to it, so the repository checks out on Windows.

`dgl/src/WindowPrivateData.cpp` and `dgl/src/pugl-upstream/src/mac.m` (Mend
patch): a standalone window is asked before closing on macOS too (it already
was on Windows and Linux), so the editor can offer to save unsaved changes.

`distrho/extra/FileBrowserDialogImpl.cpp` (Mend patch): save dialogs on
Windows and macOS start with the suggested file name, and on Windows ask
before replacing an existing file.

DPF implements VST3 and AU with its own code, so no Steinberg or Apple SDK
source is included. See `external/DPF/LICENSING.md` for the per-format details.

"VST" is a trademark of Steinberg Media Technologies GmbH. Mend is not
affiliated with or endorsed by Steinberg or any other company.

## Fonts

The logo's wordmark is set in DejaVu Sans, converted to outlines. DejaVu
fonts are derived from Bitstream Vera and are free to use, modify and
redistribute (see https://dejavu-fonts.github.io/License.html).

## DeepFilterNet

De-noise's Voice mode uses the DeepFilterNet3 model weights, used under the
MIT option of DeepFilterNet's MIT/Apache-2.0 dual licence. Mend's own code
runs them (`core/src/DeepFilter.cpp`); no DeepFilterNet code is included.

```
The MIT License (MIT)
Copyright (c) 2021 Hendrik Schröter

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

Schröter, H., Rosenkranz, T., Escalante-B., A. N., Maier, A.: "DeepFilterNet:
Perceptually Motivated Real-Time Speech Enhancement", Interspeech 2023.
