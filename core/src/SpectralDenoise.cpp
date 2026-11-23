// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#include "mend/SpectralDenoise.hpp"
#include "mend/Biquad.hpp" // gainToDb etc.

#include <algorithm>
#include <cmath>
#include <cstdio>
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

// Temporal cepstrum smoothing of the a priori SNR.
constexpr double kXiMinDb = -25.0;    // lower limit on the a priori SNR
constexpr double kAlphaHigh = 0.97;   // smoothing of the fine structure (the noise)
constexpr double kAlphaPitch = 0.2;   // light smoothing on a voice's pitch peak
constexpr double kTcsBiasDb = 2.5;    // log-domain smoothing underestimates power
// Noise tracking (adaptive mode).
constexpr double kSppPriorDb = 15.0;  // typical SNR when speech is present
constexpr double kSppTau = 0.0727;    // seconds

// Exponential integral E1(x), x > 0 (for the log-spectral amplitude gain).
double expint1(double x)
{
    if (x < 1.0) { // series
        double sum = 0.0, term = 1.0;
        for (int k = 1; k < 30; ++k) {
            term *= -x / k;
            sum += term / k;
        }
        return -0.5772156649015329 - std::log(x) - sum;
    }
    // continued fraction (Lentz)
    double b = x + 1.0, c = 1e30, d = 1.0 / b, h = d;
    for (int i = 1; i < 60; ++i) {
        const double an = -static_cast<double>(i) * i;
        b += 2.0;
        d = 1.0 / (an * d + b);
        c = b + an / c;
        const double del = c * d;
        h *= del;
        if (std::fabs(del - 1.0) < 1e-10)
            break;
    }
    return h * std::exp(-x);
}

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
    const int n = stft_.fftSize();
    for (auto& c : ch_) {
        c.prevGain.assign(bins_, 1.0);
        c.prevPost.assign(bins_, 1.0);
        c.cep.assign(static_cast<size_t>(n), 0.0);
        c.alphaQ.assign(static_cast<size_t>(n), 0.0);
    }
    fft_.prepare(n);
    realBuf_.assign(static_cast<size_t>(n), 0.0);
    specBuf_.assign(static_cast<size_t>(bins_), {});
    fftScratch_.assign(static_cast<size_t>(n), {});
    spp_.assign(bins_, 0.0);
    sppBar_.assign(bins_, 0.5);
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
        c.cepInit = false;
    }
    std::fill(spp_.begin(), spp_.end(), 0.0);
    std::fill(sppBar_.begin(), sppBar_.end(), 0.5);
    sppFrames_ = 0;
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
    const double lsaMix = std::clamp(2.0 * smooth - 1.0, 0.0, 1.0);

    // Gather the mono power for tracking and display.
    for (int k = 0; k < numBins; ++k)
        framePower_[k] += std::norm(X[k]);

    const double* noise = s.learn ? nullptr : activeNoise();
    Channel& c = ch_[channel];

    double inPow = 0.0, outPow = 0.0;
    if (noise) {
        for (int k = 0; k < numBins; ++k)
            gainSmoothed_[k] = beta * noise[k] + kTiny; // scaled noise (scratch until the smoothing below)

        // A priori SNR. Temporal cepstrum smoothing (Breithaupt, Gerkmann &
        // Martin 2008) keeps chance noise peaks from turning into tones; where
        // it sees signal we use the faster decision-directed estimate, which
        // follows onsets better, and elsewhere the lower of the two.
        const double xiMin = std::pow(10.0, kXiMinDb / 10.0);
        tcsPrior(c, X, gainSmoothed_.data(), xiMin, std::min(0.995, kAlphaHigh + (smooth - 0.5) * 0.04),
                 0.5 + 0.4 * smooth);
        for (int k = 0; k < numBins; ++k) {
            const double post = std::norm(X[k]) / gainSmoothed_[k];
            const double dd = alpha * c.prevGain[k] * c.prevGain[k] * c.prevPost[k]
                            + (1.0 - alpha) * std::max(post - 1.0, 0.0);
            const double t = gain_[k];
            const double w = std::clamp(std::log10(t) + 0.5, 0.0, 1.0);
            const double xi = w * dd + (1.0 - w) * std::min(dd, t);
            // Wiener gain, moving towards the log-spectral amplitude gain
            // (Ephraim & Malah 1985) at high smoothing settings: quieter, steadier
            // residual noise, at some cost in how much is removed.
            const double gw = xi / (1.0 + xi);
            double g = gw;
            if (lsaMix > 0.0) {
                const double glsa = std::min(1.0, gw * std::exp(0.5 * expint1(std::max(gw * post, 1e-8))));
                g = std::pow(std::max(gw, kTiny), 1.0 - lsaMix) * std::pow(std::max(glsa, kTiny), lsaMix);
            }
            gain_[k] = std::clamp(g, floorGain, 1.0);
            c.prevPost[k] = post;
        }

        // across frequency: in weak frames, smooth more (Esch & Vary 2009), so
        // lone noise peaks don't stand out as tones
        double pIn = 0.0, pNoise = 0.0;
        for (int k = 0; k < numBins; ++k) {
            pIn += std::norm(X[k]);
            pNoise += noise[k];
        }
        const double frameSnrDb = 10.0 * std::log10((pIn + kTiny) / (pNoise + kTiny));
        const double weak = std::clamp((10.0 - frameSnrDb) / 10.0, 0.0, 1.0);
        const double sp = std::min(0.9, 0.3 * smooth + 0.6 * weak);
        for (int k = 0; k < numBins; ++k) {
            const double l = gain_[k > 0 ? k - 1 : k], r = gain_[k + 1 < numBins ? k + 1 : k];
            gainSmoothed_[k] = (1.0 - sp) * gain_[k] + sp * 0.5 * (l + r);
        }
        for (int k = 0; k < numBins; ++k) {
            const double g = std::clamp(gainSmoothed_[k], floorGain, 1.0);
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

void SpectralDenoise::tcsPrior(Channel& c, const std::complex<double>* X, const double* noise, double xiMin,
                               double alphaHi, double alphaEnv)
{
    const int n = stft_.fftSize();
    const int half = n / 2;
    // log of the maximum-likelihood speech power, floored
    for (int k = 0; k <= half; ++k) {
        const double lam = std::max(std::norm(X[k]) - noise[k], xiMin * noise[k]);
        specBuf_[static_cast<size_t>(k)] = std::log(lam + kTiny);
    }
    fft_.inverseReal(specBuf_.data(), realBuf_.data(), fftScratch_.data()); // cepstrum
    // quefrency-dependent smoothing: little for the spectral envelope (< 1 ms)
    // and for a voice's pitch peak, a lot for everything else (the noise)
    const int qEnv = std::max(1, static_cast<int>(0.001 * sampleRate_));
    const int qLo = static_cast<int>(sampleRate_ / 500.0), qHi = std::min(half - 2, static_cast<int>(sampleRate_ / 70.0));
    int pitch = -1;
    double best = 0.2;
    for (int q = qLo; q <= qHi; ++q)
        if (realBuf_[static_cast<size_t>(q)] > best) {
            best = realBuf_[static_cast<size_t>(q)];
            pitch = q;
        }
    for (int q = 0; q < n; ++q) {
        const int qs = std::min(q, n - q);
        double a = qs <= qEnv ? alphaEnv : alphaHi;
        if (pitch > 0 && std::abs(qs - pitch) <= 1)
            a = kAlphaPitch;
        // the smoothing constants themselves move smoothly (beta = 0.96)
        double& aq = c.alphaQ[static_cast<size_t>(q)];
        aq = c.cepInit ? 0.96 * aq + 0.04 * a : a;
        double& cq = c.cep[static_cast<size_t>(q)];
        cq = c.cepInit ? aq * cq + (1.0 - aq) * realBuf_[static_cast<size_t>(q)] : realBuf_[static_cast<size_t>(q)];
        realBuf_[static_cast<size_t>(q)] = cq;
    }
    c.cepInit = true;
    fft_.forwardReal(realBuf_.data(), specBuf_.data(), fftScratch_.data());
    const double kappa = std::pow(10.0, kTcsBiasDb / 10.0);
    for (int k = 0; k <= half; ++k) {
        const double lam = std::exp(specBuf_[static_cast<size_t>(k)].real()) * kappa;
        gain_[static_cast<size_t>(k)] = std::max(lam / noise[k], xiMin);
    }
}

void SpectralDenoise::updateSpp()
{
    // Unbiased MMSE noise power via speech presence probability (Gerkmann &
    // Hendriks 2012), with their safeguard against stagnation.
    const double a = std::exp(-stft_.hopSize() / sampleRate_ / kSppTau);
    const double xi = std::pow(10.0, kSppPriorDb / 10.0);
    if (sppFrames_ < 8) { // start from the average of the first frames
        for (int k = 0; k < bins_; ++k)
            spp_[k] = (spp_[k] * sppFrames_ + framePower_[k]) / (sppFrames_ + 1);
        ++sppFrames_;
        return;
    }
    for (int k = 0; k < bins_; ++k) {
        const double sig = std::max(spp_[k], kTiny);
        const double post = framePower_[k] / sig;
        double p = 1.0 / (1.0 + (1.0 + xi) * std::exp(-std::min(post * xi / (1.0 + xi), 200.0)));
        sppBar_[k] = 0.9 * sppBar_[k] + 0.1 * p;
        if (sppBar_[k] > 0.99)
            p = std::min(p, 0.99);
        const double en = (1.0 - p) * framePower_[k] + p * sig;
        spp_[k] = a * sig + (1.0 - a) * en;
    }
    ++sppFrames_;
}

void SpectralDenoise::updateNoiseTracking()
{
    updateSpp();
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
        // The speech-presence tracker follows changing noise faster and without
        // bias, but can creep up under long held notes; the minimum-statistics
        // estimate caps it there.
        if (sppFrames_ >= 8 && m < 1e29)
            adaptive_[k] = std::min(spp_[k], adaptive_[k]);
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
