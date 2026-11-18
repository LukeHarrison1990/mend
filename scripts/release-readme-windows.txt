Mend @VERSION@ for Windows (64-bit)

MendEditor.exe  The editor: open a WAV, AIFF, FLAC, MP3, Ogg, Opus, AAC/ALAC
             (.m4a) or WMA file, see it as a waveform and spectrogram,
             select (or box-select) a problem and repair it. Drag a file
             onto MendEditor.exe to open it.

VST3/        Mend plugins in VST3 format (copy each .vst3 FOLDER)
CLAP/        The same plugins in CLAP format
Standalone/  Each tool as a small app that processes live audio (input to
             output, through the system default devices). Audio input
             starts OFF: click "Audio input off - click to enable" at the
             top of the window. Use headphones if the input is a microphone.

Tools: De-clip, De-click, De-crackle, De-hum, De-wind, De-plosive, De-noise,
De-reverb, Breath Control, De-ess, Centre Extract, EQ, Leveler.

Mend is free software under the MIT licence (LICENSE). Bundled components
are listed in THIRD_PARTY_NOTICES.md. Source, documentation and bug reports:
https://github.com/LukeHarrison1990/mend

Installing the plugins
  VST3: copy the folders in VST3\ to  C:\Program Files\Common Files\VST3
  CLAP: copy the files in CLAP\ to    C:\Program Files\Common Files\CLAP
Then rescan plugins in your DAW.

These builds aren't code-signed yet, so Windows SmartScreen may warn the
first time you run an .exe ("More info", then "Run anyway").
