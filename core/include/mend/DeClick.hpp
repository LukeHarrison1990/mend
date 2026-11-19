// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#pragma once

#include "mend/ArModel.hpp"
#include "mend/BlockRepairer.hpp"

#include <vector>

namespace mend {

// Detects and repairs clicks, pops and crackle.
//
// Detection: an AR model of the local signal predicts each sample; clicks
// show up as bursts of prediction error far above the robust (median-based)
// error level. Running the predictor forwards finds where a click starts,
// running it backwards finds where it ends. Bursts longer than the maximum
// click width are left alone, which protects genuine transients like drum
// hits and consonants. Repair: least-squares AR interpolation.
class DeClick : public BlockRepairer {
public:
    struct Settings {
        double sensitivity = 50.0;  // 0..100, higher finds quieter clicks
        double maxWidthMs = 1.0;    // longest burst treated as a click
        bool clicksOnly = false;    // output what is removed instead
    };

    void prepare(double sampleRate, int numChannels);
    void reset();
    void setSettings(const Settings& s) noexcept { settings_ = s; }
    const Settings& settings() const noexcept { return settings_; }

    // Smoothed rate of repaired clicks, per second, across all channels.
    double clicksPerSecond() const noexcept { return clickRate_; }
    // Total clicks repaired since reset (busiest channel; for tests and statistics).
    long totalClicks() const noexcept { return totalClicks_; }

protected:
    void repairBlock(int channel, double* x) override;
    void renderBlock(int channel, const double* repaired, const double* original, float* dst) override;
    void endBlock() override;

private:
    double robustSigma(const std::vector<double>& e, int from, int to);
    bool trySplitClick(int channel, double* x, int start, int widthLimit, double thrF, int span, int& next,
                       int& gapStart, int& gapEnd);

    Settings settings_;
    double sampleRate_ = 48000.0;
    int order_ = 32;
    int maxGap_ = 0;
    int pre_ = 1, post_ = 2;
    ArModel ar_;
    std::vector<double> ef_, eb_, scratch_;
    std::vector<int> channelClicks_;
    std::vector<int> holdoff_; // per channel: samples of the next block still protected
    int blockClicks_ = 0;
    double clickRate_ = 0.0;
    long totalClicks_ = 0;
};

} // namespace mend
