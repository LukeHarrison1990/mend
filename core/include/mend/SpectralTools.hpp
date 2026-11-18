// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
//
// STFT-based repair tools: De-reverb, De-wind and Center Extract.
#pragma once

#include "mend/Stft.hpp"

#include <complex>
#include <vector>

namespace mend {

// Reduces room reverberation. The late reverb in each frequency bin is
// predicted from the signal's own recent past, decayed at the room's
// reverb time (Lebart, Boucher & Denbigh 2001); a spectral gain removes
// that predicted tail and keeps the direct sound and early reflections.
class DeReverb : private Stft::FrameProcessor {
public:
    struct Settings {
        double reverbTime = 0.8;     // T60, seconds
        double amount = 70.0;        // 0..100
        double maxReductionDb = 15.0;
    };
    void prepare(double sampleRate, int numChannels);
    void reset();
    void setSettings(const Settings& s) noexcept { settings_ = s; }
    int latency() const noexcept { return stft_.latency(); }
    void process(const float* const* in, float* const* out, int numChannels, int numSamples)
    {
        stft_.process(in, out, numChannels, numSamples, *this);
    }

private:
    void processFrame(std::complex<double>* bins, int numBins, int channel) override;
    Settings settings_;
    double fs_ = 48000.0;
    int nb_ = 0, delayFrames_ = 1;
    Stft stft_;
    struct Ch {
        std::vector<double> psd, gain;
        std::vector<std::vector<double>> history; // delayed PSD frames
        int histPos = 0;
    };
    std::vector<Ch> ch_;
};

// Reduces wind rumble. Natural sounds rarely have far more energy below
// the cutoff than just above it; wind does. Each low bin is limited to an
// allowance derived from the band above the cutoff (rising 6 dB per
// octave downwards), so wind gusts are pulled down while voices and
// instruments keep their low end.
class DeWind : private Stft::FrameProcessor {
public:
    struct Settings {
        double frequency = 200.0;    // cutoff
        double strength = 70.0;      // 0..100
        double maxReductionDb = 30.0;
    };
    void prepare(double sampleRate, int numChannels);
    void reset();
    void setSettings(const Settings& s) noexcept { settings_ = s; }
    int latency() const noexcept { return stft_.latency(); }
    void process(const float* const* in, float* const* out, int numChannels, int numSamples)
    {
        stft_.process(in, out, numChannels, numSamples, *this);
    }

private:
    void processFrame(std::complex<double>*, int, int) override {}
    void processFrames(std::complex<double>* const* bins, int numBins, int numChannels) override;
    Settings settings_;
    double fs_ = 48000.0;
    Stft stft_;
    std::vector<double> power_, gain_;
};

// Keeps or removes what is common to both channels of a stereo file (the
// centre: usually dialogue or a lead vocal). Per bin, the similarity of
// left and right decides how much of their average is "centre".
class CenterExtract : private Stft::FrameProcessor {
public:
    struct Settings {
        bool removeCenter = false; // false: keep only the centre
        double strength = 50.0;    // 0..100: how strictly "centre" is judged
        double amount = 100.0;     // 0..100 wet mix
    };
    void prepare(double sampleRate, int numChannels);
    void reset();
    void setSettings(const Settings& s) noexcept { settings_ = s; }
    int latency() const noexcept { return stft_.latency(); }
    void process(const float* const* in, float* const* out, int numChannels, int numSamples)
    {
        stft_.process(in, out, numChannels, numSamples, *this);
    }

private:
    void processFrame(std::complex<double>*, int, int) override {}
    void processFrames(std::complex<double>* const* bins, int numBins, int numChannels) override;
    Settings settings_;
    Stft stft_;
    std::vector<double> sim_;
};

int stftSizeFor(double sampleRate, double seconds); // power of two near `seconds`

} // namespace mend
