// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#include "mend/DeClip.hpp"
#include "mend/Biquad.hpp"

#include <algorithm>
#include <cmath>

namespace mend {

namespace {
int blockSizeFor(double sampleRate)
{
    int b = 1024;
    while (b < sampleRate * 0.04) // ~40 ms: clipped runs can be long
        b <<= 1;
    return b;
}
constexpr int kMinRun = 2;           // shorter runs are just peaks
constexpr double kTolerance = 0.999; // within 0.1% of the clip level
constexpr int kIterations = 2;
} // namespace

void DeClip::prepare(double sampleRate, int numChannels)
{
    sampleRate_ = sampleRate;
    const int B = blockSizeFor(sampleRate);
    const double scale = sampleRate / 48000.0;
    order_ = std::clamp(static_cast<int>(std::lround(64 * std::sqrt(scale))), 32, 128);
    maxGap_ = std::min(static_cast<int>(sampleRate * 0.008), B - 3 * order_); // 8 ms
    ar_.prepare(order_, maxGap_, 2 * B);
    gaps_.reserve(B);
    skipUntil_.assign(numChannels, 0);
    prepareBlocks(B, numChannels);
    reset();
}

void DeClip::reset()
{
    resetBlocks();
    std::fill(skipUntil_.begin(), skipUntil_.end(), 0);
    peakHold_ = 0.0;
    level_ = dbToGain(settings_.thresholdDb);
    blockRepaired_ = totalRepaired_ = 0;
    clippedPct_ = 0.0;
}

double DeClip::clipLevelDb() const noexcept
{
    return gainToDb(level_);
}

void DeClip::beginBlock()
{
    if (!settings_.autoThreshold) {
        level_ = dbToGain(settings_.thresholdDb);
        return;
    }
    // Track the loudest recent peak: clipping sits at a fixed ceiling.
    const int B = blockSize();
    double m = 0.0;
    for (int c = 0; c < numChannels(); ++c) {
        const double* r = raw(c);
        for (int i = B; i < 2 * B; ++i)
            m = std::max(m, std::abs(r[i]));
    }
    // decay ~6 dB per 10 s so a quieter later section is re-measured
    const double decay = std::pow(0.5, (B / sampleRate_) / 10.0);
    peakHold_ = std::max(m, peakHold_ * decay);
    level_ = std::max(peakHold_, 1e-4);
}

void DeClip::repairBlock(int channel, double* x)
{
    const int B = blockSize();
    const double* r = raw(channel);
    const double thr = level_ * kTolerance;

    // Find runs that start in the current block (they may extend on).
    gaps_.clear();
    // Skip samples already repaired as the tail of a run in the last block.
    int i = std::max(B, skipUntil_[channel] - B);
    skipUntil_[channel] = 0;
    while (i < 2 * B) {
        if (std::abs(r[i]) < thr) {
            ++i;
            continue;
        }
        int j = i;
        const double sign = r[i] >= 0 ? 1.0 : -1.0;
        while (j < 3 * B - order_ && std::abs(r[j]) >= thr && (r[j] >= 0 ? 1.0 : -1.0) == sign)
            ++j;
        const int len = j - i;
        if (len >= kMinRun && len <= maxGap_ && i >= order_ && j + order_ <= 3 * B) {
            gaps_.push_back({ i, j });
            skipUntil_[channel] = std::max(skipUntil_[channel], j);
        }
        i = j;
    }
    if (gaps_.empty())
        return;

    for (int it = 0; it < kIterations; ++it) {
        if (!ar_.fit(x, 2 * B, order_))
            return;
        for (const Gap& g : gaps_) {
            ar_.interpolate(x, g.g0, g.g1);
            // The true peak was at least as loud as the clip level.
            for (int t = g.g0; t < g.g1; ++t) {
                const double s = r[t] >= 0 ? 1.0 : -1.0;
                if (x[t] * s < level_)
                    x[t] = s * level_;
            }
        }
    }
    for (const Gap& g : gaps_)
        blockRepaired_ += g.g1 - g.g0;
}

void DeClip::renderBlock(int, const double* repaired, const double*, float* dst)
{
    const double g = dbToGain(settings_.outputGainDb);
    for (int i = 0; i < blockSize(); ++i)
        dst[i] = static_cast<float>(repaired[i] * g);
}

void DeClip::endBlock()
{
    const double blockSeconds = blockSize() / sampleRate_;
    const double instant = 100.0 * blockRepaired_ / (static_cast<double>(blockSize()) * numChannels());
    const double coeff = 1.0 - std::exp(-blockSeconds / 0.5);
    clippedPct_ += (instant - clippedPct_) * coeff;
    totalRepaired_ += blockRepaired_;
    blockRepaired_ = 0;
}

} // namespace mend
