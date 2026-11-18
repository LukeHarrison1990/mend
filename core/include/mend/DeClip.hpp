// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#pragma once

#include "mend/ArModel.hpp"
#include "mend/BlockRepairer.hpp"

#include <vector>

namespace mend {

// Rebuilds the flattened peaks of clipped audio.
//
// Runs of samples stuck at (or beyond) the clip level are treated as
// missing and re-estimated with least-squares AR interpolation, then
// constrained to lie beyond the clip level with the original polarity
// (the true waveform must have been at least that loud). The fit and
// repair are iterated so that the model is learned from repaired audio.
//
// Restored peaks exceed the original clip level, so the output gain
// control gives headroom.
class DeClip : public BlockRepairer {
public:
    struct Settings {
        bool autoThreshold = true;   // find the clip level automatically
        double thresholdDb = -0.1;   // manual clip level, dBFS
        double outputGainDb = 0.0;   // applied after repair
    };

    void prepare(double sampleRate, int numChannels);
    void reset();
    void setSettings(const Settings& s) noexcept { settings_ = s; }
    const Settings& settings() const noexcept { return settings_; }

    // Clip level in use, dBFS.
    double clipLevelDb() const noexcept;
    // Smoothed percentage of samples being repaired.
    double clippedPercent() const noexcept { return clippedPct_; }
    long totalRepairedSamples() const noexcept { return totalRepaired_; }

protected:
    void beginBlock() override;
    void repairBlock(int channel, double* x) override;
    void renderBlock(int channel, const double* repaired, const double* original, float* dst) override;
    void endBlock() override;

private:
    struct Gap { int g0, g1; };

    Settings settings_;
    double sampleRate_ = 48000.0;
    int order_ = 32, maxGap_ = 0;
    ArModel ar_;
    std::vector<Gap> gaps_;
    std::vector<int> skipUntil_; // per channel, in 3B buffer coordinates
    double peakHold_ = 0.0;
    double level_ = 1.0;
    long blockRepaired_ = 0, totalRepaired_ = 0;
    double clippedPct_ = 0.0;
};

} // namespace mend
