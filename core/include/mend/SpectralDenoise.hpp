// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#pragma once

#include "mend/Stft.hpp"

#include <complex>
#include <string>
#include <vector>

namespace mend {

// Removes steady broadband noise (hiss, hum-free room tone, fans, air-con).
//
// Works on the short-time spectrum. For every frequency bin it needs an
// estimate of the noise level, which comes from either:
//   * a learned profile: average spectrum of a noise-only passage, captured
//     while Learn is on (the classic workflow), or
//   * adaptive tracking: the noise floor is followed continuously using
//     minimum statistics (the lowest smoothed level over ~1.5 s is noise,
//     since speech and music keep moving and noise doesn't).
//
// The gain per bin is a Wiener-style rule driven by a "decision-directed"
// a-priori SNR estimate (Ephraim & Malah 1984), which suppresses the
// warbling "musical noise" that plain spectral subtraction leaves behind.
// Gains never drop below the reduction floor, and are smoothed over time
// and across neighbouring bins.
class SpectralDenoise : private Stft::FrameProcessor {
public:
    struct Settings {
        double thresholdDb = 0.0;   // -10..+20: how much louder than the noise
                                    // profile something must be to be kept
        double reductionDb = 12.0;  // maximum attenuation of noise
        double smoothing = 50.0;    // 0..100: less warble vs faster response
        bool adaptive = true;       // track the noise continuously
        bool learn = false;         // capture a noise profile while on
        bool noiseOnly = false;     // output what is removed instead
    };

    void prepare(double sampleRate, int numChannels);
    void reset();
    void setSettings(const Settings& s) noexcept { settings_ = s; }
    const Settings& settings() const noexcept { return settings_; }

    int latency() const noexcept { return stft_.latency(); }
    int numBins() const noexcept { return stft_.numBins(); }
    int fftSize() const noexcept { return stft_.fftSize(); }
    double sampleRate() const noexcept { return sampleRate_; }

    void process(const float* const* in, float* const* out, int numChannels, int numSamples);

    // Learned profile
    bool hasLearnedProfile() const noexcept { return hasLearned_; }
    void clearLearnedProfile() noexcept { hasLearned_ = false; }
    // Text form (for plugin state): sample rate, FFT size and per-bin levels.
    std::string profileToString() const;
    // Returns false (and leaves the profile alone) if the text is invalid.
    bool profileFromString(const std::string& text);

    // Display: smoothed per-bin power (linear) of the input, output and the
    // noise estimate in use. Sized numBins().
    const std::vector<float>& displayInput() const noexcept { return dispIn_; }
    const std::vector<float>& displayOutput() const noexcept { return dispOut_; }
    const std::vector<float>& displayNoise() const noexcept { return dispNoise_; }
    // The noise estimate currently in use (per-bin power), or empty while
    // there is none yet. For tests.
    std::vector<double> currentNoiseEstimate() const
    {
        const double* n = activeNoise();
        return n ? std::vector<double>(n, n + bins_) : std::vector<double>{};
    }
    // How much the noise is being reduced over the last block, dB
    // (positive): the gains weighted by the noise spectrum.
    double reductionAppliedDb() const noexcept { return appliedDb_; }

private:
    void processFrame(std::complex<double>* bins, int numBins, int channel) override;
    void updateNoiseTracking();
    const double* activeNoise() const noexcept;

    Settings settings_;
    double sampleRate_ = 48000.0;
    int numChannels_ = 2;
    int activeChannels_ = 1;
    Stft stft_;
    int bins_ = 0;

    // per-frame mono power, gathered across channels
    std::vector<double> framePower_;
    int channelsInFrame_ = 0;

    // learned profile
    std::vector<double> learned_, learnAccum_;
    long learnFrames_ = 0;
    bool wasLearning_ = false;
    bool hasLearned_ = false;

    // adaptive (minimum statistics)
    std::vector<double> smoothPow_, subMin_, adaptive_;
    std::vector<std::vector<double>> minHistory_;
    int subFrame_ = 0, subWindowFrames_ = 0, historyPos_ = 0;
    long adaptiveFrames_ = 0;

    // per-channel gain state
    struct Channel {
        std::vector<double> prevGain, prevPost;
    };
    std::vector<Channel> ch_;
    std::vector<double> gain_, gainSmoothed_;

    std::vector<float> dispIn_, dispOut_, dispNoise_;
    double appliedSum_ = 0.0;
    long appliedCount_ = 0;
    double appliedDb_ = 0.0;
};

} // namespace mend
