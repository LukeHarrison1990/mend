// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
//
// Offline ("utility") processing for the editor's one-shot modules: they
// see the whole selection at once rather than streaming through it.
// Every function works on non-interleaved float channels.
#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace mend::offline {

using Channels = std::vector<std::vector<float>>;
// Optional progress (0..1) and cancellation; return false to stop.
using Progress = std::function<bool(double)>;

inline size_t frames(const Channels& c) { return c.empty() ? 0 : c[0].size(); }

// ---- level ------------------------------------------------------------------
void applyGain(Channels& x, double db);
double samplePeak(const Channels& x); // linear
// Scale so the highest sample (or each channel's) peak hits targetDb.
// Returns the gain applied in dB (the largest, if per channel).
double normalizePeak(Channels& x, double targetDb, bool perChannel);

enum class FadeCurve { Linear, EqualPower, SCurve, Exponential, Logarithmic };
void applyFade(Channels& x, bool fadeIn, FadeCurve curve);

// ---- phase and channels -------------------------------------------------------
void removeDc(Channels& x);
void invertPolarity(Channels& x, int channel); // -1: all channels
// Rotate the phase of every frequency by `degrees` (Hilbert transform).
// Output is aligned with the input.
void rotatePhase(Channels& x, double degrees);
// The rotation (0..175 degrees) giving the lowest peak.
double bestPhaseRotation(const Channels& x);

enum class MixOp { SwapLR, MonoSum, MidSideEncode, MidSideDecode };
void mixOp(Channels& x, MixOp op);
void channelGains(Channels& x, double leftDb, double rightDb);

struct Azimuth {
    double delaySamples = 0.0; // how late R is relative to L
    double gainDb = 0.0;       // how much quieter R is than L
    double correlation = 0.0;  // 0..1, confidence
};
Azimuth measureAzimuth(const Channels& x, double fs, double maxDelayMs = 2.0);
// Delay or advance R by -delaySamples (fractional), raise it by gainDb.
void correctAzimuth(Channels& x, double delaySamples, double gainDb);

// ---- repair ------------------------------------------------------------------
// Replace x[gapStart, gapStart+gapLen) with least-squares AR interpolation
// from the audio either side (which must be present in x).
bool interpolateGap(Channels& x, size_t gapStart, size_t gapLen);

enum class SignalType { Silence, Sine, WhiteNoise, PinkNoise, Bleep };
void generate(Channels& x, double fs, SignalType type, double freq, double levelDb, unsigned seed = 1);

// ---- loudness ------------------------------------------------------------------
struct Loudness {
    double integrated = -200.0; // LUFS (BS.1770-4, gated)
    double range = 0.0;         // LU (EBU Tech 3342)
    double shortTermMax = -200.0;
    double truePeakDb = -200.0; // dBTP (4x oversampled)
    double samplePeakDb = -200.0;
};
Loudness measureLoudness(const Channels& x, double fs);
double truePeakDb(const Channels& x, double fs);
// Brick-wall limiter (offline look-ahead) so true peaks stay under ceilingDb.
void limitPeaks(Channels& x, double fs, double ceilingDb);

// ---- time and rate -------------------------------------------------------------
Channels resample(const Channels& x, double fsIn, double fsOut, const Progress& progress = {});
// Shorten every silence longer than minSilenceSec to keepSec.
// Returns the new audio; `removed` (optional) gets the frames removed.
Channels trimSilence(const Channels& x, double fs, double thresholdDb, double minSilenceSec, double keepSec,
                     size_t* removed = nullptr);

// ---- spectrum -------------------------------------------------------------------
// Average spectrum (dB per FFT bin, 1/6-octave smoothed) and the bin width.
std::vector<double> averageSpectrumDb(const Channels& x, double fs, int fftSize, double* binHz = nullptr);
// Filter x so its average spectrum moves towards `referenceDb` (from
// averageSpectrumDb with the same FFT size). amount 0..1, change limited
// to +/- maxDb. Linear phase, aligned with the input.
void eqMatch(Channels& x, double fs, const std::vector<double>& referenceDb, int fftSize, double amount, double maxDb);

// Linear-phase FIR convolution (FFT overlap-add); output aligned with input
// when the FIR is symmetric around its centre.
void convolveCentered(std::vector<float>& x, const std::vector<double>& fir);

// ---- statistics ---------------------------------------------------------------
struct ChannelStats {
    double peakDb = -200, rmsDb = -200, dcPercent = 0, crestDb = 0, truePeakDb = -200;
    size_t clippedRuns = 0; // runs of 3+ samples at full scale
};
std::vector<ChannelStats> channelStats(const Channels& x, double fs);

// ---- export -------------------------------------------------------------------
// Quantise to `bits` with TPDF dither, optionally noise-shaped, leaving the
// values as floats exactly on the integer grid (so the WAV writer's
// conversion is lossless).
void ditherTo(Channels& x, int bits, bool noiseShaped, unsigned seed = 1);

// ---- spectral -------------------------------------------------------------------
// Keep `processed` only between loHz and hiHz; elsewhere take `original`.
// Both must be the same size. Soft band edges.
void spectralBlend(Channels& processed, const Channels& original, double fs, double loHz, double hiHz);

enum class SpectralMode { Attenuate, Replace, Gain };
// Repair the box [start, start+length) x [loHz, hiHz) of x, using the audio
// before and after it (inside x) as the reference. strength 0..1.
void spectralRepair(Channels& x, double fs, size_t start, size_t length, double loHz, double hiHz, SpectralMode mode,
                    double strength, double gainDb);

} // namespace mend::offline
