# Changelog

All notable changes to Mend. Versions follow [Semantic Versioning](https://semver.org/);
while below 1.0, minor versions may change plugin parameters.

## 0.9.0 (2026-11-19)

### Added
- Audio settings in every standalone app: choose the input and output
  device from those the system offers, which input channels to use (a
  pair, or one input to both sides for a single microphone), the sample
  rate and the buffer size. Click the pill at the top of the window.
  Changes apply straight away, are saved, and are shared by all the Mend
  apps. If a chosen device is unplugged, the app carries on with the
  system default and switches back when it returns.
- Editor: an **Audio** button to choose the output device and buffer size.

### Changed
- Standalone apps now use miniaudio for their audio (WASAPI on Windows,
  Core Audio on macOS, PulseAudio or ALSA on Linux), like the editor. On
  Linux they still join JACK when it is running.

### Fixed
- macOS: the standalone apps were missing from the release (the
  Standalone folder was empty). They are now proper app bundles
  ("Mend Voice.app" and so on) with the microphone permission text macOS
  needs for audio input.

## 0.8.1 (2026-11-19)

### Changed
- Voice: the microphone profiles for the Shure SM7B and SM58, AKG C414 XLS,
  Electro-Voice RE20, Neumann TLM 103, AEA R84, Sennheiser MD 421 and
  MKE 400, Audio-Technica AT2020USB+, and Blue Snowball iCE and Yeti are now
  built from the manufacturers' published frequency-response charts (sources
  in the README). The remaining profiles are estimates and are now gentler
  (2 dB at most). The C414 profile is now named "AKG C414 XLS".

## 0.8.0 (2026-11-19)

### Added
- **Voice**, a new tool (plugin, standalone app and editor tool): one-step
  speech polish with Clarity, Smooth and De-mud controls, five characters
  and correction profiles for 28 microphones, from studio mics to USB mics,
  earbuds and phones.
- Installers: a Windows setup program and a macOS package, each showing the
  licence and letting you choose the editor, the standalone apps and each
  plugin format separately.
- Editor: projects (`.mend`) that keep the audio and every setting; Save
  (Ctrl+S) and Save As (Ctrl+Shift+S).
- Editor: asks whether to save unsaved changes before closing or opening
  another file.
- Editor: a slider blends the view from waveform to spectrogram.
- Controls under automatic control (De-hum's frequency while tracking,
  De-clip's threshold in auto mode) move with what was detected, in the
  plugins and the editor; editor tool cards show live meters while
  previewing.

### Changed
- Editor: files open in the background with a progress bar and Cancel,
  instead of freezing the window.
- Editor: scrolling large files is much smoother (the waveform is drawn
  once into an image wider than the view, at a quarter of the memory).
- Export WAV is now Ctrl+E (Ctrl+S saves the project).

## 0.7.0 (2026-11-18)

### Added
- Eight new plugins and standalone apps: De-crackle, De-ess, Breath Control,
  De-wind, De-reverb, Centre Extract, EQ and Leveler (VST3, CLAP, LV2, AU).
- Logo and app icon: embedded in every Windows `.exe`, in the macOS app
  bundle, and installed with a `.desktop` file on Linux; drawn in every
  tool's header.
- Release packaging (`scripts/package.sh`) and CI that builds release
  archives for Windows, macOS and Linux when a version tag is pushed.

### Fixed
- De-click now repairs clicks that fall within a few milliseconds of each
  other; before, such pairs could be mistaken for a transient and left in.

## 0.6.0
- Editor: spectral (box) selection; Spectral Repair; thirteen repair tools in
  the chain; fourteen utility modules (Gain, Normalize, Fade, Loudness, EQ
  Match, Phase, Mixing, Azimuth, Interpolate, Signal Generator, Trim Silence,
  Resample, Waveform Stats, Export Format with dither).

## 0.5.0
- Editor: spectrogram view (Mel, Log and Linear scales), smooth selection on
  4K screens, import of AIFF, Ogg Vorbis, Opus, AAC, ALAC and WMA.

## 0.4.0
- Mend Editor: open a file, waveform view, run several tools at once with
  live preview, background render, undo and redo, WAV export.

## 0.3.0
- Spectral De-noise. Sharp UI at any display scaling.

## 0.2.0
- De-hum, De-click, De-clip and De-plosive as plugins and standalone apps.
