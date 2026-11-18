// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#include "mend/Stft.hpp"
#include "mend/Biquad.hpp" // kPi

#include <algorithm>
#include <cmath>

namespace mend {

void Stft::prepare(int fftSize, int overlap, int numChannels)
{
    n_ = fftSize;
    hop_ = fftSize / std::max(2, overlap);
    numChannels_ = numChannels;
    fft_.prepare(fftSize);

    // Periodic Hann for analysis and synthesis (its square overlap-adds
    // flat at 75% overlap and above); square-root Hann at 50% overlap.
    window_.resize(n_);
    for (int i = 0; i < n_; ++i) {
        const double h = 0.5 - 0.5 * std::cos(2.0 * kPi * i / n_);
        window_[i] = (n_ / hop_ <= 2) ? std::sqrt(h) : h;
    }

    // Normalise so the overlapped window-squared sum is exactly 1.
    // (Hann squared sums to a constant at these overlaps, so any offset works.)
    double sum = 0.0;
    for (int i = hop_ / 2; i < n_; i += hop_)
        sum += window_[i] * window_[i];
    norm_ = 1.0 / sum;

    ch_.assign(numChannels, Channel{});
    for (auto& c : ch_) {
        c.in.assign(n_, 0.0);
        c.out.assign(n_, 0.0);
    }
    frame_.assign(n_, 0.0);
    bins_.assign(numChannels, std::vector<std::complex<double>>(n_ / 2 + 1));
    binPtrs_.resize(numChannels);
    for (int c = 0; c < numChannels; ++c)
        binPtrs_[c] = bins_[c].data();
    scratch_.assign(n_, {});
    reset();
}

void Stft::reset()
{
    for (auto& c : ch_) {
        std::fill(c.in.begin(), c.in.end(), 0.0);
        std::fill(c.out.begin(), c.out.end(), 0.0);
    }
    pos_ = 0;
    hopCounter_ = 0;
}

void Stft::runFrames(int numChannels, FrameProcessor& fp)
{
    for (int c = 0; c < numChannels; ++c) {
        const Channel& ch = ch_[c];
        // oldest sample first
        for (int i = 0; i < n_; ++i)
            frame_[i] = ch.in[(pos_ + i) % n_] * window_[i];
        fft_.forwardReal(frame_.data(), bins_[c].data(), scratch_.data());
    }
    fp.processFrames(binPtrs_.data(), n_ / 2 + 1, numChannels);
    for (int c = 0; c < numChannels; ++c) {
        Channel& ch = ch_[c];
        fft_.inverseReal(bins_[c].data(), frame_.data(), scratch_.data());
        for (int i = 0; i < n_; ++i)
            ch.out[(pos_ + i) % n_] += frame_[i] * window_[i] * norm_;
    }
}

void Stft::process(const float* const* in, float* const* out, int numChannels, int numSamples,
                   FrameProcessor& fp)
{
    numChannels = std::min(numChannels, numChannels_);
    for (int s = 0; s < numSamples; ++s) {
        for (int c = 0; c < numChannels; ++c) {
            Channel& ch = ch_[c];
            ch.in[pos_] = in[c][s];
            out[c][s] = static_cast<float>(ch.out[pos_]);
            ch.out[pos_] = 0.0;
        }
        pos_ = (pos_ + 1) % n_;
        if (++hopCounter_ >= hop_) {
            hopCounter_ = 0;
            runFrames(numChannels, fp);
        }
    }
}

} // namespace mend
