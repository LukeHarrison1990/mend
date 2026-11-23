// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
//
// Neural speech noise reduction: a dependency-free C++ implementation of
// DeepFilterNet3 (Schröter et al., "DeepFilterNet: Perceptually Motivated
// Real-Time Speech Enhancement", 2023; MIT or Apache-2.0).
//
// It is discriminative, not generative: a small network estimates a gain
// for each of 32 frequency bands and short complex filters for the lowest
// 96 bins (up to 4.8 kHz), and these are applied to the noisy spectrum
// itself. Nothing is synthesised.
//
// Runs at 48 kHz in hops of 480 samples (10 ms). With several channels the
// network listens to their mix and the same gains and filters are applied
// to each, which keeps a stereo image steady. Output is delayed by
// latency() samples: 480 for the STFT plus 2 hops of look-ahead.
#pragma once

#include <array>
#include <complex>
#include <cstddef>
#include <vector>

namespace mend {

class DeepFilterNet {
public:
    static constexpr int kSampleRate = 48000;
    static constexpr int kFft = 960;
    static constexpr int kHop = 480;
    static constexpr int kBins = kFft / 2 + 1; // 481
    static constexpr int kErb = 32;
    static constexpr int kDf = 96;       // bins with deep filtering
    static constexpr int kOrder = 5;     // deep filter taps (frames)
    static constexpr int kLookahead = 2; // frames
    static constexpr int kChannels = 64;
    static constexpr int kHidden = 256;
    static constexpr int kEmb = 512;

    static constexpr int latency() { return kFft - kHop + kLookahead * kHop; } // 1440

    // Number of float weights the model needs (in DeepFilter.cpp's order).
    static std::size_t weightCount();

    DeepFilterNet();
    ~DeepFilterNet();
    DeepFilterNet(const DeepFilterNet&) = delete;
    DeepFilterNet& operator=(const DeepFilterNet&) = delete;

    // Point the network at its weights (not copied; must outlive this).
    // Returns false if `count` is wrong.
    bool setWeights(const float* weights, std::size_t count);
    bool ready() const noexcept { return w_ != nullptr; }

    static constexpr int kMaxChannels = 8;
    void setChannels(int n);
    int channels() const noexcept { return nch_; }
    void reset();

    // How far noise may be turned down, dB. >= 100 means no limit.
    void setAttenuationLimitDb(float db) noexcept;
    // Extra suppression in very noisy frames (DeepFilterNet's post-filter).
    void setPostFilter(bool on, float beta = 0.02f) noexcept
    {
        postFilter_ = on;
        pfBeta_ = beta;
    }

    // Process one hop: kHop input samples in, kHop output samples out
    // (the output belongs to the input of latency() samples ago).
    // Returns the network's local SNR estimate for the frame, dB.
    float processHop(const float* const* in, float* const* out) noexcept;
    float processHop(const float* in, float* out) noexcept { return processHop(&in, &out); } // one channel

    // The last band gains (0..1, kErb values) and local SNR, for meters.
    const float* bandGains() const noexcept { return mask_.data(); }
    float localSnrDb() const noexcept { return lsnr_; }

    // Thresholds on the local SNR (dB) that decide which stages run,
    // as in the reference implementation.
    float minDbThresh = -15.0f, maxDbErbThresh = 35.0f, maxDbDfThresh = 35.0f;

private:
    using cf = std::complex<float>;
    struct Weights;
    struct Fft;

    struct Chan;
    void analysis(Chan& ch, const float* in, cf* spec) noexcept;
    void synthesis(Chan& ch, cf* spec, float* out) noexcept;
    void encoder() noexcept;
    void erbDecoder() noexcept;
    void dfDecoder() noexcept;

    const float* w_ = nullptr;
    Weights* W_ = nullptr;
    Fft* fft_ = nullptr;

    std::array<float, kFft> window_ {};
    std::array<int, kErb> erbWidth_ {};
    int nch_ = 0;
    std::vector<Chan*> ch_;
    std::vector<cf> mix_; // the channels' mean spectrum (what the network hears)

    // features and their normalisation state
    std::array<float, kErb> meanNorm_ {};
    std::array<float, kDf> unitNorm_ {};
    float alpha_ = 0.99f;


    // network state
    std::array<std::array<float, kErb>, 3> featErbHist_ {};       // last 3 frames
    std::array<std::array<float, 2 * kDf>, 3> featSpecHist_ {};   // re[96], im[96]
    std::vector<float> e0_, e1_, e2_, e3_, c0_, emb_;
    std::vector<std::vector<float>> c0Hist_; // last kOrder frames of c0
    std::vector<float> hEnc_, hErb0_, hErb1_, hDf0_, hDf1_;
    std::array<float, kErb> mask_ {};
    std::vector<float> coefs_; // kDf * kOrder * 2
    float lsnr_ = 0.0f;

    float attenLim_ = 0.0f; // linear gain mixed back from the noisy input; 0 = no limit
    bool postFilter_ = false;
    float pfBeta_ = 0.02f;
    int skipCounter_ = 0;

    // scratch
    std::vector<float> s0_, s1_, s2_, s3_;
    std::vector<cf> frame_, out_;
    std::vector<float> time_;
};

} // namespace mend
