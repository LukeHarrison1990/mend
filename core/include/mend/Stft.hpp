// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#pragma once

#include "mend/Fft.hpp"

#include <complex>
#include <vector>

namespace mend {

// Streaming short-time Fourier transform with overlap-add resynthesis.
// The foundation for the spectral tools (de-noise, spectral repair, the
// editor's spectrogram). Hann analysis and synthesis windows; with no
// spectral change the output is the input delayed by latency() samples.
//
// Usage: subclass, or pass a FrameProcessor, and edit the bins of each
// frame. Allocation-free after prepare().
class Stft {
public:
    struct FrameProcessor {
        virtual ~FrameProcessor() = default;
        // bins: fftSize/2 + 1 complex values for one channel's frame.
        virtual void processFrame(std::complex<double>* bins, int numBins, int channel) = 0;
        // All channels of one frame together (for tools that compare
        // channels). By default each channel is handed to processFrame.
        virtual void processFrames(std::complex<double>* const* bins, int numBins, int numChannels)
        {
            for (int c = 0; c < numChannels; ++c)
                processFrame(bins[c], numBins, c);
        }
    };

    // overlap: 2 (50%), 4 (75%) or 8 (87.5%)
    void prepare(int fftSize, int overlap, int numChannels);
    void reset();

    int fftSize() const noexcept { return n_; }
    int hopSize() const noexcept { return hop_; }
    int numBins() const noexcept { return n_ / 2 + 1; }
    int latency() const noexcept { return n_; }

    void process(const float* const* in, float* const* out, int numChannels, int numSamples,
                 FrameProcessor& fp);

private:
    void runFrames(int numChannels, FrameProcessor& fp);

    Fft fft_;
    int n_ = 0, hop_ = 0, numChannels_ = 0;
    std::vector<double> window_;
    double norm_ = 1.0;

    struct Channel {
        std::vector<double> in, out;
    };
    std::vector<Channel> ch_;
    int pos_ = 0, hopCounter_ = 0;

    std::vector<double> frame_;
    std::vector<std::vector<std::complex<double>>> bins_;
    std::vector<std::complex<double>*> binPtrs_;
    std::vector<std::complex<double>> scratch_;
};

} // namespace mend
