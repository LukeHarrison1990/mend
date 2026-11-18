// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#include "mend/SpectralDenoise.hpp"
#include "mend/Biquad.hpp" // gainToDb etc.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace mend {

namespace {
constexpr int kOverlap = 4;
constexpr double kMinStatSmoothing = 0.85;  // periodogram smoothing for minimum tracking
constexpr int kSubWindows = 4;              // minimum taken over 4 sub-windows...
constexpr double kSubWindowSeconds = 0.4;   // ...of 0.4 s each (~1.6 s)
constexpr double kMinStatBias = 2.4;        // minimum of a smoothed periodogram underestimates the mean (calibrated on white noise)
constexpr double kDisplaySmoothing = 0.7;
constexpr double kTiny = 1e-20;

int fftSizeFor(double sampleRate)
{
    // ~43 ms frames: 2048 at 44.1/48 kHz, 4096 at 88.2/96 kHz, ...
    int n = 2048;
    while (n * 48000.0 / sampleRate < 1800.0)
        n <<= 1;
    return n;
}
} // namespace

void SpectralDenoise::prepare(double sampleRate, int numChannels)
{
    sampleRate_ = sampleRate;
    numChannels_ = std::max(1, numChannels);
    stft_.prepare(fftSizeFor(sampleRate), kOverlap, numChannels_);
    bins_ = stft_.numBins();

    framePower_.assign(bins_, 0.0);
    if (static_cast<int>(learned_.size()) != bins_) {
        learned_.assign(bins_, 0.0);
        hasLearned_ = false;
    }
    learnAccum_.assign(bins_, 0.0);
    smoothPow_.assign(bins_, 0.0);
    subMin_.assign(bins_, 0.0);
    adaptive_.assign(bins_, 0.0);
    minHistory_.assign(kSubWindows, std::vector<double>(bins_, 0.0));
    const double framesPerSecond = sampleRate / stft_.hopSize();
    subWindowFrames_ = std::max(4, static_cast<int>(kSubWindowSeconds * framesPerSecond));

    ch_.assign(numChannels_, Channel{});
    for (auto& c : ch_) {
        c.prevGain.assign(bins_, 1.0);
        c.prevPost.assign(bins_, 1.0);
    }
    gain_.assign(bins_, 1.0);
    gainSmoothed_.assign(bins_, 1.0);
    dispIn_.assign(bins_, 0.0f);
    dispOut_.assign(bins_, 0.0f);
    dispNoise_.assign(bins_, 0.0f);
    reset();
}

void SpectralDenoise::reset()
{
    stft_.reset();
    std::fill(framePower_.begin(), framePower_.end(), 0.0);
    channelsInFrame_ = 0;
    std::fill(learnAccum_.begin(), learnAccum_.end(), 0.0);
    learnFrames_ = 0;
    wasLearning_ = false;
    std::fill(smoothPow_.begin(), smoothPow_.end(), 0.0);
    std::fill(subMin_.begin(), subMin_.end(), 1e30);
    for (auto& h : minHistory_)
        std::fill(h.begin(), h.end(), 1e30);
    std::fill(adaptive_.begin(), adaptive_.end(), 0.0);
    subFrame_ = historyPos_ = 0;
    adaptiveFrames_ = 0;
    for (auto& c : ch_) {
        std::fill(c.prevGain.begin(), c.prevGain.end(), 1.0);
        std::fill(c.prevPost.begin(), c.prevPost.end(), 1.0);
    }
    appliedSum_ = 0.0;
    appliedCount_ = 0;
    appliedDb_ = 0.0;
}

const double* SpectralDenoise::activeNoise() const noexcept
{
    if (settings_.adaptive && adaptiveFrames_ > subWindowFrames_)
        return adaptive_.data();
    if (!settings_.adaptive && hasLearned_)
        return learned_.data();
    return nullptr; // nothing to go on yet: pass audio through
}

void SpectralDenoise::process(const float* const* in, float* const* out, int numChannels, int numSamples)
{
    numChannels = std::min(numChannels, numChannels_);
    activeChannels_ = numChannels;
    appliedSum_ = 0.0;
    appliedCount_ = 0;

    // Finish a learning pass when Learn is switched off.
    if (wasLearning_ && !settings_.learn) {
        if (learnFrames_ >= 8) {
            for (int k = 0; k < bins_; ++k)
                learned_[k] = learnAccum_[k] / static_cast<double>(learnFrames_);
            hasLearned_ = true;
        }
        std::fill(learnAccum_.begin(), learnAccum_.end(), 0.0);
        learnFrames_ = 0;
    }
    wasLearning_ = settings_.learn;

    stft_.process(in, out, numChannels, numSamples, *this);

    if (appliedCount_ > 0)
        appliedDb_ = appliedSum_ / static_cast<double>(appliedCount_);
}

void SpectralDenoise::processFrame(std::complex<double>* X, int numBins, int channel)
{
    const Settings& s = settings_;
    const double smooth = std::clamp(s.smoothing, 0.0, 100.0) / 100.0;
    const double beta = std::pow(10.0, std::clamp(s.thresholdDb, -20.0, 30.0) / 10.0);
    const double floorGain = dbToGain(-std::clamp(s.reductionDb, 0.0, 80.0));
    const double alpha = 0.92 + 0.06 * smooth;          // decision-directed weight
    const double fall = 1.0 - 0.8 * smooth;             // how fast gains may drop
    const double spread = 0.5 * smooth;                 // neighbour-bin smoothing

    // Gather the mono power for tracking and display.
    for (int k = 0; k < numBins; ++k)
        framePower_[k] += std::norm(X[k]);

    const double* noise = s.learn ? nullptr : activeNoise();
    Channel& c = ch_[channel];

    double inPow = 0.0, outPow = 0.0;
    if (noise) {
        for (int k = 0; k < numBins; ++k) {
            const double P = std::norm(X[k]);
            const double N = beta * noise[k] + kTiny;
            const double post = P / N;
            const double prior = alpha * c.prevGain[k] * c.prevGain[k] * c.prevPost[k]
                               + (1.0 - alpha) * std::max(post - 1.0, 0.0);
            gain_[k] = std::max(prior / (1.0 + prior), floorGain);
            c.prevPost[k] = post;
        }
        // across frequency
        for (int k = 0; k < numBins; ++k) {
            const double l = gain_[k > 0 ? k - 1 : k], r = gain_[k + 1 < numBins ? k + 1 : k];
            gainSmoothed_[k] = (1.0 - spread) * gain_[k] + spread * 0.5 * (l + r);
        }
        // over time: rise at once (keeps transients), fall gradually (no flicker)
        for (int k = 0; k < numBins; ++k) {
            double g = gainSmoothed_[k];
            const double prev = c.prevGain[k];
            if (g < prev)
                g = prev + (g - prev) * fall;
            g = std::clamp(g, floorGain, 1.0);
            c.prevGain[k] = g;
            const double P = std::norm(X[k]);
            inPow += noise[k];             // how much of the *noise* is removed:
            outPow += noise[k] * g * g;    // noise-weighted attenuation
            X[k] *= s.noiseOnly ? (1.0 - g) : g;
            if (channel == 0)
                dispOut_[k] = static_cast<float>(kDisplaySmoothing * dispOut_[k] + (1.0 - kDisplaySmoothing) * P * g * g);
        }
    } else {
        for (int k = 0; k < numBins; ++k) {
            c.prevGain[k] = 1.0;
            c.prevPost[k] = 1.0;
            if (s.noiseOnly)
                X[k] = 0.0;
            if (channel == 0)
                dispOut_[k] = static_cast<float>(kDisplaySmoothing * dispOut_[k]
                                                 + (1.0 - kDisplaySmoothing) * (s.noiseOnly ? 0.0 : std::norm(X[k])));
        }
    }
    if (inPow > kTiny) {
        appliedSum_ += 10.0 * std::log10(inPow / std::max(outPow, kTiny));
        ++appliedCount_;
    }

    if (++channelsInFrame_ >= activeChannels_) {
        channelsInFrame_ = 0;
        for (int k = 0; k < numBins; ++k)
            framePower_[k] /= activeChannels_;
        updateNoiseTracking();
        std::fill(framePower_.begin(), framePower_.end(), 0.0);
    }
}

void SpectralDenoise::updateNoiseTracking()
{
    // learning: plain average
    if (settings_.learn) {
        for (int k = 0; k < bins_; ++k)
            learnAccum_[k] += framePower_[k];
        ++learnFrames_;
    }

    // minimum statistics
    for (int k = 0; k < bins_; ++k) {
        smoothPow_[k] = adaptiveFrames_ == 0
            ? framePower_[k]
            : kMinStatSmoothing * smoothPow_[k] + (1.0 - kMinStatSmoothing) * framePower_[k];
        subMin_[k] = std::min(subMin_[k], smoothPow_[k]);
    }
    ++adaptiveFrames_;
    if (++subFrame_ >= subWindowFrames_) {
        subFrame_ = 0;
        minHistory_[historyPos_] = subMin_;
        historyPos_ = (historyPos_ + 1) % kSubWindows;
        std::fill(subMin_.begin(), subMin_.end(), 1e30);
    }
    for (int k = 0; k < bins_; ++k) {
        double m = subMin_[k];
        for (const auto& h : minHistory_)
            m = std::min(m, h[k]);
        adaptive_[k] = m < 1e29 ? m * kMinStatBias : 0.0;
    }

    // display
    const double* noise = activeNoise();
    for (int k = 0; k < bins_; ++k) {
        dispIn_[k] = static_cast<float>(kDisplaySmoothing * dispIn_[k] + (1.0 - kDisplaySmoothing) * framePower_[k]);
        dispNoise_[k] = noise ? static_cast<float>(noise[k]) : 0.0f;
    }
}

std::string SpectralDenoise::profileToString() const
{
    if (!hasLearned_)
        return {};
    std::ostringstream os;
    os << "MENDNP1 " << static_cast<long>(sampleRate_) << ' ' << fftSize();
    char buf[32];
    for (int k = 0; k < bins_; ++k) {
        std::snprintf(buf, sizeof(buf), " %.2f", 10.0 * std::log10(learned_[k] + kTiny));
        os << buf;
    }
    return os.str();
}

bool SpectralDenoise::profileFromString(const std::string& text)
{
    std::istringstream is(text);
    std::string magic;
    long fs0 = 0;
    int n0 = 0;
    if (!(is >> magic >> fs0 >> n0) || magic != "MENDNP1" || fs0 <= 0 || n0 < 16 || n0 > (1 << 20))
        return false;
    std::vector<double> src(n0 / 2 + 1);
    for (double& v : src) {
        double db;
        if (!(is >> db))
            return false;
        v = std::pow(10.0, db / 10.0);
    }
    // Map onto this instance's bins by frequency (the sample rate or FFT
    // size may differ from when the profile was learned). Per-bin power
    // of steady noise scales with the FFT size.
    const double srcHzPerBin = static_cast<double>(fs0) / n0;
    const double hzPerBin = sampleRate_ / fftSize();
    const double scale = static_cast<double>(fftSize()) / n0;
    for (int k = 0; k < bins_; ++k) {
        const double pos = k * hzPerBin / srcHzPerBin;
        const int i = static_cast<int>(pos);
        double v;
        if (i >= static_cast<int>(src.size()) - 1)
            v = src.back();
        else
            v = src[i] + (src[i + 1] - src[i]) * (pos - i);
        learned_[k] = v * scale;
    }
    hasLearned_ = true;
    return true;
}

} // namespace mend
