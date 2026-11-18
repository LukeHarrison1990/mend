// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
//
// Level-based dialogue tools: De-ess, Leveler and Breath Control.
#pragma once

#include "mend/Biquad.hpp"
#include "mend/Fft.hpp"

#include <cmath>
#include <complex>
#include <vector>

namespace mend {

// Fixed delay line per channel (look-ahead for the dynamics tools).
class DelayLines {
public:
    void prepare(int numChannels, int delay);
    void reset();
    int delay() const noexcept { return delay_; }
    // Push x for channel c, return the sample from `delay` samples ago.
    inline float push(int c, float x) noexcept
    {
        auto& b = buf_[static_cast<size_t>(c)];
        const float y = b[static_cast<size_t>(pos_[c])];
        b[static_cast<size_t>(pos_[c])] = x;
        if (++pos_[c] >= delay_ + 1)
            pos_[c] = 0;
        return delay_ == 0 ? x : y;
    }

private:
    int delay_ = 0;
    std::vector<std::vector<float>> buf_;
    std::vector<int> pos_;
};

// Reduces sibilance ("s", "sh", "t"). The band above `frequency` is split
// off with a Linkwitz-Riley crossover; when its level rises above the
// threshold the band (or, in wideband mode, the whole signal) is turned
// down, by up to `maxReductionDb`. 1 ms of look-ahead catches the onset.
class DeEss {
public:
    struct Settings {
        double thresholdDb = -30.0; // level of the sibilant band where reduction starts
        double frequency = 5000.0;  // crossover
        double maxReductionDb = 12.0;
        bool wideband = false;
    };
    void prepare(double sampleRate, int numChannels);
    void reset();
    void setSettings(const Settings& s) noexcept { settings_ = s; }
    int latency() const noexcept { return delay_.delay(); }
    void process(const float* const* in, float* const* out, int numChannels, int numSamples);
    double currentReductionDb() const noexcept { return grDb_; }

private:
    void design();
    static constexpr int kMaxCh = 8;
    Settings settings_;
    double fs_ = 48000.0, designed_ = -1.0;
    int nch_ = 1;
    struct Ch {
        Biquad lp1, lp2, hp1, hp2;
    };
    std::vector<Ch> ch_;
    DelayLines delayLo_, delay_;
    double env_ = 0.0, envFull_ = 0.0, grDb_ = 0.0;
    double att_ = 0, rel_ = 0, grAtt_ = 0, grRel_ = 0;
};

// Evens out dialogue level: a slow automatic gain that brings the
// loudness (K-weighted, 400 ms) towards a target, within limits. Quiet
// gaps below the gate don't pull the gain up. Look-ahead centres the
// measurement on the audio it controls.
class Leveler {
public:
    struct Settings {
        double targetDb = -20.0;
        double maxBoostDb = 12.0;
        double maxCutDb = 12.0;
        double speedSeconds = 1.0; // how quickly the gain follows
        double gateDb = -50.0;
    };
    void prepare(double sampleRate, int numChannels);
    void reset();
    void setSettings(const Settings& s) noexcept { settings_ = s; }
    int latency() const noexcept { return delay_.delay(); }
    void process(const float* const* in, float* const* out, int numChannels, int numSamples);
    double currentGainDb() const noexcept { return gainDb_; }

private:
    Settings settings_;
    double fs_ = 48000.0;
    int nch_ = 1;
    std::vector<Biquad> shelf_, hp_;
    DelayLines delay_;
    std::vector<double> window_; // K-weighted mean square ring (one value per sample)
    size_t winPos_ = 0;
    double winSum_ = 0.0;
    int counter_ = 0;
    double gainDb_ = 0.0, targetGainDb_ = 0.0;
};

// Finds breaths between phrases and turns them down. Each 20 ms frame is
// classed as a breath when it is unvoiced (no pitch), noise-like across
// the speech band, not dominated by the very top (sibilance) and well
// below the recent speech level; isolated decisions are smoothed away.
class BreathControl {
public:
    struct Settings {
        double sensitivity = 50.0; // 0..100
        double reductionDb = 12.0; // 0..60
    };
    void prepare(double sampleRate, int numChannels);
    void reset();
    void setSettings(const Settings& s) noexcept { settings_ = s; }
    int latency() const noexcept { return delay_.delay(); }
    void process(const float* const* in, float* const* out, int numChannels, int numSamples);
    long breathFrames() const noexcept { return breathFrames_; }
    double currentReductionDb() const noexcept { return -20.0 * std::log10(gain_ + 1e-9); }

private:
    bool classify();
    Settings settings_;
    double fs_ = 48000.0;
    int nch_ = 1, n_ = 1024, hop_ = 256;
    Fft fft_;
    std::vector<double> ring_, frame_, win_;
    std::vector<std::complex<double>> bins_, scratch_;
    int ringPos_ = 0, hopCount_ = 0;
    std::vector<char> decisions_; // recent per-hop decisions (ring)
    int decPos_ = 0, smoothFrames_ = 9;
    double speechLevelDb_ = -60.0;
    double gain_ = 1.0, target_ = 1.0, att_ = 0, rel_ = 0;
    DelayLines delay_;
    long breathFrames_ = 0;
};

} // namespace mend
