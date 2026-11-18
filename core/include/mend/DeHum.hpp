// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#pragma once

#include "mend/Biquad.hpp"
#include "mend/HumDetector.hpp"

#include <array>
#include <vector>

namespace mend {

// Removes mains hum and its harmonics with a cascade of narrow cuts, one
// per harmonic, optionally tracking the hum's fundamental automatically.
// Zero latency, real-time safe after prepare().
class DeHum {
public:
    static constexpr int kMaxChannels = 8;
    static constexpr int kMaxHarmonics = 16;

    struct Settings {
        double frequency = 50.0;  // manual fundamental, Hz
        bool autoDetect = true;   // track the fundamental automatically
        int harmonics = 8;        // number of harmonics to cut, incl. fundamental
        double q = 40.0;          // notch sharpness; higher = narrower
        double reductionDb = 30.0;// depth of each cut
        bool humOnly = false;     // output what is being removed instead
    };

    void prepare(double sampleRate, int maxBlockSize, int numChannels);
    void reset();
    void setSettings(const Settings& s) noexcept { settings_ = s; }
    const Settings& settings() const noexcept { return settings_; }

    void process(const float* const* in, float* const* out, int numChannels, int numSamples);

    // True when filters are being applied: always in manual mode; in auto
    // mode only once hum has been found.
    bool active() const noexcept { return !settings_.autoDetect || detector_.result().everLocked; }

    // Fundamental currently in use (manual or tracked, after smoothing).
    double currentFrequency() const noexcept { return currentFreq_; }
    const HumDetector::Result& detection() const noexcept { return detector_.result(); }
    // RMS level of the removed signal over the last block, dBFS.
    double removedLevelDb() const noexcept { return removedDb_; }

    // The cascade's coefficients for given settings, shared with the UI so
    // the drawn response curve is exactly what the DSP applies.
    static int designCascade(double sampleRate, double f0, const Settings& s,
                             std::array<BiquadCoeffs, kMaxHarmonics>& out) noexcept;

private:
    void updateFilters(double f0);

    Settings settings_;
    double sampleRate_ = 48000.0;
    int numChannels_ = 2;
    HumDetector detector_;
    std::vector<float> mono_;

    std::array<std::array<Biquad, kMaxHarmonics>, kMaxChannels> filters_{};
    std::array<BiquadCoeffs, kMaxHarmonics> coeffs_{};
    int activeSections_ = 0;

    double currentFreq_ = 50.0;
    double designedFreq_ = -1.0;
    Settings designedSettings_{};
    double removedDb_ = -120.0;
    bool wasActive_ = false;
};

} // namespace mend
