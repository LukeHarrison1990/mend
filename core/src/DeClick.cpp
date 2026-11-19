// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#include "mend/DeClick.hpp"

#include <algorithm>
#include <cmath>

namespace mend {

namespace {
int blockSizeFor(double sampleRate)
{
    int b = 512;
    while (b < sampleRate * 0.02) // ~20 ms
        b <<= 1;
    return b;
}
} // namespace

void DeClick::prepare(double sampleRate, int numChannels)
{
    sampleRate_ = sampleRate;
    const int B = blockSizeFor(sampleRate);
    const double scale = sampleRate / 48000.0;
    order_ = std::clamp(static_cast<int>(std::lround(64 * std::sqrt(scale))), 32, 128);
    pre_ = std::max(1, static_cast<int>(std::lround(1 * scale)));
    post_ = std::max(2, static_cast<int>(std::lround(2 * scale)));
    // Longest repairable gap: up to 5 ms, bounded by the look-ahead block.
    maxGap_ = std::min(static_cast<int>(sampleRate * 0.005) + pre_ + post_, B - 3 * order_);
    ar_.prepare(order_, maxGap_ + order_, 2 * B);
    ef_.assign(B + maxGap_ + 2 * order_ + 4, 0.0);
    eb_.assign(B + maxGap_ + 2 * order_ + 4, 0.0);
    scratch_.assign(B, 0.0);
    holdoff_.assign(numChannels, 0);
    channelClicks_.assign(numChannels, 0);
    prepareBlocks(B, numChannels);
    reset();
}

void DeClick::reset()
{
    resetBlocks();
    std::fill(holdoff_.begin(), holdoff_.end(), 0);
    blockClicks_ = 0;
    clickRate_ = 0.0;
    totalClicks_ = 0;
}

double DeClick::robustSigma(const std::vector<double>& e, int from, int to)
{
    const int n = to - from;
    for (int i = 0; i < n; ++i)
        scratch_[i] = std::abs(e[from + i]);
    std::nth_element(scratch_.begin(), scratch_.begin() + n / 2, scratch_.begin() + n);
    return 1.4826 * scratch_[n / 2];
}

void DeClick::repairBlock(int channel, double* x)
{
    const int B = blockSize();
    const int p = order_;

    // Model the recent signal (previous, already clean, plus current).
    if (!ar_.fit(x, 2 * B, p))
        return; // silence: nothing to do

    // Residual index i corresponds to sample t = B + i.
    const int span = B + maxGap_ + p; // how far detection may look
    const double* a = ar_.coeffs();
    auto fwd = [&](int t) { return ar_.residual(x, t); };
    auto bwd = [&](int t) {
        double e = 0.0;
        for (int k = 0; k <= p; ++k)
            e += a[k] * x[t + k];
        return e;
    };
    for (int i = 0; i < span; ++i) {
        ef_[i] = fwd(B + i);
        eb_[i] = bwd(B + i);
    }

    const double sens = std::clamp(settings_.sensitivity, 0.0, 100.0);
    const double k = 12.0 * std::pow(0.2, sens / 100.0); // 12 sigma .. 2.4 sigma
    // Clicks quieter than about -65 dBFS are inaudible; without this floor,
    // perfectly predictable material (synths, test tones) gives a near-zero
    // error level and tiny modelling errors would be flagged.
    const double floor = 1e-4 * k;
    const double thrF = std::max(k * robustSigma(ef_, 0, B), floor);
    const double thrB = std::max(k * robustSigma(eb_, 0, B), floor);
    const int maxWidth = std::max(1, static_cast<int>(settings_.maxWidthMs * 1e-3 * sampleRate_));
    const int widthLimit = std::min(maxWidth, maxGap_ - pre_ - post_);

    // A click is a short, isolated burst of prediction error. Exceedances
    // closer together than the model order belong to one event; events
    // longer than the maximum click width are real transients and are
    // left alone, with a short hold-off for their ringing tail.
    const int mergeGap = p;
    const int maxEvent = widthLimit + 2 * p;
    const int holdoffLen = static_cast<int>(0.010 * sampleRate_);
    auto exceeds = [&](int j) { return std::abs(ef_[j]) > thrF || std::abs(eb_[j]) > thrB; };

    // A click repaired by splitting a chained event was estimated from audio
    // that still held the next click. Once that one is repaired too, the
    // earlier gap is filled again from the now-clean surroundings.
    int redo0 = -1, redo1 = -1;
    auto redoEarlier = [&](int nextGapStart) {
        if (redo0 >= 0 && nextGapStart < redo1 + p)
            ar_.interpolate(x, redo0, redo1);
        redo0 = -1;
    };

    int i = std::max(0, holdoff_[channel]);
    holdoff_[channel] = 0;
    while (i < B) {
        if (std::abs(ef_[i]) <= thrF) {
            ++i;
            continue;
        }
        const int start = i; // forward error jumps where the click starts
        int last = start;
        for (int j = start + 1; j < span && j - last <= mergeGap && j - start <= maxEvent; ++j)
            if (exceeds(j))
                last = j;

        // The click ends at the last backward-error exceedance in the event.
        int end = start;
        for (int j = start; j <= last; ++j)
            if (std::abs(eb_[j]) > thrB)
                end = j;

        const int width = end - start + 1;
        if (last - start + 1 > widthLimit + p || width > widthLimit) {
            // Two clicks close together chain into one long event, because
            // each one's prediction error spreads by up to the model order.
            // Try repairing just the start as a click: if the signal after it
            // is predictable again it was a click, and the scan carries on to
            // the next one. A real transient stays unpredictable, so the trial
            // repair is undone and the transient is protected as before.
            int s0 = 0, s1 = 0;
            if (trySplitClick(channel, x, start, widthLimit, thrF, span, i, s0, s1)) {
                redoEarlier(s0);
                redo0 = s0;
                redo1 = s1;
                continue;
            }
            i = last + holdoffLen; // a transient: protect it and its tail
            if (i >= B)
                holdoff_[channel] = i - B;
            continue;
        }

        const int g0 = std::max(B, B + start - pre_); // never touch audio already output
        const int g1 = B + end + 1 + post_;
        ar_.interpolate(x, g0, g1);
        redoEarlier(g0);
        ++channelClicks_[channel];

        // The repair changes the forward error just after the gap.
        for (int t = g1; t < std::min(g1 + p, B + span); ++t)
            ef_[t - B] = fwd(t);
        i = g1 - B;
    }
}

bool DeClick::trySplitClick(int channel, double* x, int start, int widthLimit, double thrF, int span, int& next,
                            int& gapStart, int& gapEnd)
{
    // Try repairs of increasing width from the start of the event and keep
    // the narrowest one after which the forward prediction error is quiet
    // again: that was a complete click. If none works, it is a transient.
    const int B = blockSize();
    const int p = order_;
    const int g0 = std::max(B, B + start - pre_);
    const int judge = std::max(8, p / 2); // samples after the gap that must be predictable
    const int step = std::max(1, widthLimit / 24);
    for (int w = 1; w <= widthLimit; w += step) {
        const int g1 = B + start + w + post_;
        const int checkTo = g1 + judge;
        if (checkTo > B + span || g1 - g0 > maxGap_)
            break; // not enough look-ahead to judge
        std::copy(x + g0, x + g1, scratch_.begin());
        ar_.interpolate(x, g0, g1);
        bool quiet = true;
        for (int t = g1; t < checkTo && quiet; ++t)
            quiet = std::abs(ar_.residual(x, t)) <= thrF;
        if (quiet) {
            ++channelClicks_[channel];
            for (int t = g1; t < std::min(g1 + p, B + span); ++t)
                ef_[t - B] = ar_.residual(x, t);
            next = g1 - B;
            gapStart = g0;
            gapEnd = g1;
            return true;
        }
        std::copy(scratch_.begin(), scratch_.begin() + (g1 - g0), x + g0); // undo
    }
    return false;
}

void DeClick::renderBlock(int, const double* repaired, const double* original, float* dst)
{
    const int B = blockSize();
    if (settings_.clicksOnly) {
        for (int i = 0; i < B; ++i)
            dst[i] = static_cast<float>(original[i] - repaired[i]);
    } else {
        for (int i = 0; i < B; ++i)
            dst[i] = static_cast<float>(repaired[i]);
    }
}

void DeClick::endBlock()
{
    // Stereo clicks usually hit both channels: report the busiest channel
    // rather than the sum, so the rate means clicks heard.
    blockClicks_ = 0;
    for (int& c : channelClicks_) {
        blockClicks_ = std::max(blockClicks_, c);
        c = 0;
    }
    const double blockSeconds = blockSize() / sampleRate_;
    const double instant = blockClicks_ / blockSeconds;
    // ~0.5 s smoothing
    const double coeff = 1.0 - std::exp(-blockSeconds / 0.5);
    clickRate_ += (instant - clickRate_) * coeff;
    totalClicks_ += blockClicks_;
}

} // namespace mend
