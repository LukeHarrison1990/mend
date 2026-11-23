// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
//
// Voice noise reduction for any sample rate and block size: wraps
// DeepFilterNet (which runs at 48 kHz in 10 ms hops) with streaming
// resampling and buffering, and uses the model weights built into Mend.
#pragma once

#include "mend/DeepFilter.hpp"

#include <memory>
#include <vector>

namespace mend {

// Fixed-ratio streaming resampler (windowed sinc, Kaiser), for one channel.
class StreamResampler {
public:
    void prepare(double fsIn, double fsOut);
    void reset();
    bool bypass() const noexcept { return bypass_; }
    // Push input samples, then pull as many output samples as available.
    void push(const float* in, int n);
    int available() const noexcept;
    int pull(float* out, int maxOut) noexcept;
    // Group delay in input samples (the filter's half length).
    int delayIn() const noexcept { return bypass_ ? 0 : W_; }

private:
    bool bypass_ = true;
    double step_ = 1.0; // input samples per output sample
    double pos_ = 0.0;  // next output position in buf_ (input samples)
    int W_ = 0, sub_ = 256;
    std::vector<float> table_, buf_;
};

class NeuralDenoise {
public:
    NeuralDenoise();
    ~NeuralDenoise();

    // True if the model weights are available (they are built in).
    static bool available();

    void prepare(double sampleRate, int numChannels, int maxBlock);
    void reset();

    // How far noise is turned down, dB. >= 100: as far as the model goes.
    void setReductionDb(float db) noexcept;
    float reductionDb() const noexcept { return reductionDb_; }

    void process(const float* const* in, float* const* out, int numChannels, int numSamples) noexcept;

    // Latency in samples at the host rate.
    int latency() const noexcept { return latency_; }

    // For meters: the model's last local SNR estimate (dB, -15..35) and
    // band gains (DeepFilterNet::kErb values, 0..1).
    float localSnrDb() const noexcept { return net_.localSnrDb(); }
    const float* bandGains() const noexcept { return net_.bandGains(); }

private:
    DeepFilterNet net_;
    double fs_ = 48000.0;
    int nch_ = 1;
    float reductionDb_ = 100.0f;
    int latency_ = 0;
    std::vector<StreamResampler> up_, down_;
    std::vector<std::vector<float>> inFifo_, outFifo_, tmp_;
    std::vector<float*> hopIn_, hopOut_;
    std::vector<std::vector<float>> hopInBuf_, hopOutBuf_;
};

} // namespace mend
