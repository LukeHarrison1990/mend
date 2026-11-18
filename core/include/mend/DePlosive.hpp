// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#pragma once

#include "mend/Biquad.hpp"

#include <array>
#include <vector>

namespace mend {

// Tames plosive "p"/"b" pops: bursts of low-frequency energy from breath
// hitting the microphone.
//
// The signal is split with a Linkwitz-Riley crossover (the bands sum back
// flat). The balance between the low band and the voice band above it is
// learned continuously; a plosive is when the low band suddenly jumps well
// past that usual balance. The low band alone is pulled back toward
// normal, with a short look-ahead so the reduction is in place before the
// pop arrives. The voice above the crossover is never touched.
class DePlosive {
public:
    static constexpr int kMaxChannels = 8;

    struct Settings {
        double sensitivity = 50.0;  // 0..100, higher catches gentler pops
        double strength = 75.0;     // 0..100, how far a pop is pulled back to normal
        double frequency = 150.0;   // crossover, Hz
        double maxReductionDb = 24.0;
        bool plosivesOnly = false;  // output what is removed instead
    };

    void prepare(double sampleRate, int numChannels);
    void reset();
    void setSettings(const Settings& s) noexcept { settings_ = s; }
    const Settings& settings() const noexcept { return settings_; }

    int latency() const noexcept { return lookahead_; }
    void process(const float* const* in, float* const* out, int numChannels, int numSamples);

    // Deepest gain reduction in the last block, dB (positive).
    double reductionDb() const noexcept { return blockMaxReduction_; }

private:
    void designCrossover();

    Settings settings_;
    double designedFreq_ = -1.0;
    double sampleRate_ = 48000.0;
    int numChannels_ = 2;
    int lookahead_ = 0;

    struct Channel {
        Biquad lp1, lp2, hp1, hp2;
        std::vector<float> lowDelay, highDelay;
    };
    std::array<Channel, kMaxChannels> ch_{};
    int delayPos_ = 0;

    double envLow_ = 0.0, envHigh_ = 0.0;
    double attackCoeff_ = 0.0, releaseCoeff_ = 0.0;
    double grDb_ = 0.0, grAttack_ = 0.0, grRelease_ = 0.0;
    double blockMaxReduction_ = 0.0;
    double typicalBalance_ = -6.0; // learned low-vs-voice balance, dB
    double learnCoeff_ = 0.0;
};

} // namespace mend
