// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#include "mend/DePlosive.hpp"

#include <algorithm>
#include <cmath>

namespace mend {

namespace {
double coeffFor(double seconds, double sampleRate)
{
    return 1.0 - std::exp(-1.0 / (seconds * sampleRate));
}
} // namespace

void DePlosive::prepare(double sampleRate, int numChannels)
{
    sampleRate_ = sampleRate;
    numChannels_ = std::clamp(numChannels, 1, kMaxChannels);
    lookahead_ = static_cast<int>(sampleRate * 0.004); // 4 ms
    for (auto& c : ch_) {
        c.lowDelay.assign(lookahead_ + 1, 0.0f);
        c.highDelay.assign(lookahead_ + 1, 0.0f);
    }
    // Envelope followers: fast attack to see the pop's onset, ~25 ms release.
    attackCoeff_ = coeffFor(0.0003, sampleRate);
    releaseCoeff_ = coeffFor(0.025, sampleRate);
    // Gain: reach full reduction within the look-ahead, recover over 60 ms.
    grAttack_ = coeffFor(0.001, sampleRate);
    grRelease_ = coeffFor(0.060, sampleRate);
    learnCoeff_ = coeffFor(2.0, sampleRate); // usual balance learned over ~2 s
    designedFreq_ = -1.0;
    reset();
}

void DePlosive::reset()
{
    for (auto& c : ch_) {
        c.lp1.reset(); c.lp2.reset(); c.hp1.reset(); c.hp2.reset();
        std::fill(c.lowDelay.begin(), c.lowDelay.end(), 0.0f);
        std::fill(c.highDelay.begin(), c.highDelay.end(), 0.0f);
    }
    delayPos_ = 0;
    envLow_ = envHigh_ = 0.0;
    grDb_ = 0.0;
    blockMaxReduction_ = 0.0;
    typicalBalance_ = -6.0;
}

void DePlosive::designCrossover()
{
    const double f = std::clamp(settings_.frequency, 30.0, sampleRate_ * 0.2);
    if (std::abs(f - designedFreq_) < 1e-6)
        return;
    // Linkwitz-Riley 4th order = two cascaded Butterworth 2nd order sections.
    const auto lp = BiquadCoeffs::lowpass(sampleRate_, f, 0.70710678);
    const auto hp = BiquadCoeffs::highpass(sampleRate_, f, 0.70710678);
    for (auto& c : ch_) {
        c.lp1.setCoeffs(lp); c.lp2.setCoeffs(lp);
        c.hp1.setCoeffs(hp); c.hp2.setCoeffs(hp);
    }
    designedFreq_ = f;
}

void DePlosive::process(const float* const* in, float* const* out, int numChannels, int numSamples)
{
    numChannels = std::min(numChannels, numChannels_);
    designCrossover();

    const double sens = std::clamp(settings_.sensitivity, 0.0, 100.0);
    // How far above its usual balance the low band may jump before it's a
    // pop: 18 dB at sensitivity 0, 0 dB at 100.
    const double margin = 18.0 * (1.0 - sens / 100.0);
    const double strength = std::clamp(settings_.strength, 0.0, 100.0) / 100.0;
    const double maxRed = std::clamp(settings_.maxReductionDb, 0.0, 60.0);
    constexpr double knee = 3.0; // dB over which detection fades in
    const int len = lookahead_ + 1;
    double blockMax = 0.0;

    for (int s = 0; s < numSamples; ++s) {
        double peakLow = 0.0, peakHigh = 0.0;
        for (int c = 0; c < numChannels; ++c) {
            Channel& ch = ch_[c];
            const double x = in[c][s];
            const double lo = ch.lp2.process(ch.lp1.process(x));
            const double hi = ch.hp2.process(ch.hp1.process(x));
            ch.lowDelay[delayPos_] = static_cast<float>(lo);
            ch.highDelay[delayPos_] = static_cast<float>(hi);
            peakLow = std::max(peakLow, std::abs(lo));
            peakHigh = std::max(peakHigh, std::abs(hi));
        }

        envLow_ += (peakLow - envLow_) * (peakLow > envLow_ ? attackCoeff_ : releaseCoeff_);
        envHigh_ += (peakHigh - envHigh_) * (peakHigh > envHigh_ ? attackCoeff_ : releaseCoeff_);

        const double lowDb = gainToDb(envLow_);
        const double highDb = gainToDb(envHigh_);
        const double balance = lowDb - highDb;
        double target = 0.0;
        if (lowDb > -60.0) {
            // Pull a pop back toward this voice's usual balance.
            const double excess = balance - (typicalBalance_ + margin);
            const double w = std::clamp(excess / knee, 0.0, 1.0);
            target = std::clamp(w * strength * (balance - typicalBalance_), 0.0, maxRed);
        }
        // Learn the usual low/voice balance while nothing is being reduced.
        if (target < 0.1 && grDb_ < 0.5 && lowDb > -60.0 && highDb > -60.0)
            typicalBalance_ += (balance - typicalBalance_) * learnCoeff_;

        grDb_ += (target - grDb_) * (target > grDb_ ? grAttack_ : grRelease_);
        blockMax = std::max(blockMax, grDb_);
        const double g = dbToGain(-grDb_);

        // The bands sum back to the input's level at every frequency (a
        // Linkwitz-Riley crossover is all-pass), with only the low band
        // turned down while a pop is reduced.
        const int readPos = (delayPos_ + 1) % len; // oldest sample = lookahead ago
        for (int c = 0; c < numChannels; ++c) {
            const Channel& ch = ch_[c];
            const double lo = ch.lowDelay[readPos];
            const double hi = ch.highDelay[readPos];
            out[c][s] = static_cast<float>(settings_.plosivesOnly ? lo * (1.0 - g) : lo * g + hi);
        }
        delayPos_ = (delayPos_ + 1) % len;
    }
    blockMaxReduction_ = blockMax;
}

} // namespace mend
