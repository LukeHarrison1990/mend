Mend @VERSION@ for Linux (x86-64)

MendEditor   The editor: open an audio file, see it as a waveform and
             spectrogram, select (or box-select) a problem and repair it.
             AAC, ALAC and WMA need FFmpeg installed.
LV2/         LV2 plugins

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

Installing: run ./install.sh to put the plugins in ~/.vst3, ~/.clap and
~/.lv2, the apps in ~/.local/bin, and Mend Editor in your applications menu.
