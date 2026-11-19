# Changelog

All notable changes to Mend. Versions follow [Semantic Versioning](https://semver.org/);
while below 1.0, minor versions may change plugin parameters.

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
