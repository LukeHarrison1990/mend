// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#pragma once

#include "mend/Biquad.hpp"

#include <array>
#include <vector>

namespace mend {

// Six-band parametric EQ: high-pass, low shelf, three peaking bands, high
// shelf and low-pass. A band at 0 dB (or a filter at its "off" end) is
// skipped entirely. Zero latency.
class ParametricEq {
public:
    struct Settings {
        double highPassHz = 20.0;   // <= 20 Hz: off
        double lowShelfHz = 100.0, lowShelfDb = 0.0;
        std::array<double, 3> peakHz { 300.0, 1000.0, 4000.0 };
        std::array<double, 3> peakDb { 0.0, 0.0, 0.0 };
        std::array<double, 3> peakQ { 1.0, 1.0, 1.0 };
        double highShelfHz = 8000.0, highShelfDb = 0.0;
        double lowPassHz = 20000.0; // >= 20 kHz: off
        bool operator==(const Settings& o) const noexcept;
    };

    void prepare(double sampleRate, int numChannels);
    void reset();
    void setSettings(const Settings& s);
    const Settings& settings() const noexcept { return settings_; }

    void process(const float* const* in, float* const* out, int numChannels, int numSamples);

    // Magnitude response in dB at `hz` with the current settings.
    double responseDb(double hz) const;

private:
    void design();

    static constexpr int kMaxSections = 9; // HP and LP are 4th order (two sections each)
    Settings settings_;
    double sampleRate_ = 48000.0;
    int numChannels_ = 0;
    std::vector<BiquadCoeffs> active_;
    std::vector<std::array<Biquad, kMaxSections>> filters_;
};

} // namespace mend
