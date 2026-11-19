Mend @VERSION@ for macOS (Apple silicon and Intel)

The installer (Mend-@VERSION@-macos.pkg on the Releases page) does everything
below for you and lets you choose which parts to install.

MendEditor.app  The editor: open an audio file, see it as a waveform and
             spectrogram, select (or box-select) a problem and repair it.
AU/          Audio Unit plugins (Logic, GarageBand, ...)
LV2/         LV2 plugins (if present)

VST3/        Mend plugins in VST3 format (copy each .vst3 FOLDER)
CLAP/        The same plugins in CLAP format
Standalone/  Each tool as a small app that processes live audio (input to
             output, through the system default devices). Audio input
             starts OFF: click "Audio input off - click to enable" at the
             top of the window. Use headphones if the input is a microphone.

Tools: De-clip, De-click, De-crackle, De-hum, De-wind, De-plosive, De-noise,
De-reverb, Breath Control, De-ess, Centre Extract, EQ, Leveler, Voice.

Mend is free software under the MIT licence (LICENSE). Bundled components
are listed in THIRD_PARTY_NOTICES.md. Source, documentation and bug reports:
https://github.com/LukeHarrison1990/mend

Installing the plugins
  AU:   copy to ~/Library/Audio/Plug-Ins/Components
  VST3: copy to ~/Library/Audio/Plug-Ins/VST3
  CLAP: copy to ~/Library/Audio/Plug-Ins/CLAP

These builds aren't signed or notarised yet. Open the apps with right-click >
Open the first time. If a DAW won't load the plugins, run in Terminal:
  xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins
