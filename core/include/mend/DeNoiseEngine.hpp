// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
//
// The De-noise tool's two engines behind one interface, shared by the
// plugin and the editor:
//   * Any sound: SpectralDenoise (noise profile, for music, ambience, archives)
//   * Voice:     NeuralDenoise (DeepFilterNet3, for speech)
// Latency is the same in both modes, so switching never shifts the audio,
// and a switch crossfades over 40 ms.
#pragma once

#include "mend/NeuralDenoise.hpp"
#include "mend/SpectralDenoise.hpp"

#include <atomic>
#include <vector>

namespace mend {

class DeNoiseEngine {
public:
    enum class Mode { AnySound = 0, Voice = 1 };

    void prepare(double sampleRate, int numChannels, int maxBlock);
    void reset();

    // Takes effect at the start of the next process() call.
    void setMode(Mode m) noexcept { pending_.store(static_cast<int>(m), std::memory_order_relaxed); }
    Mode mode() const noexcept { return static_cast<Mode>(pending_.load(std::memory_order_relaxed)); }
    // Any-sound settings (its own noiseOnly is ignored: see setNoiseOnly).
    void setSpectralSettings(const SpectralDenoise::Settings& s) noexcept;
    // Voice: how far noise is turned down, dB; >= 60 means as far as it goes.
    void setVoiceReductionDb(float db) noexcept;
    // Output what is removed instead of what is kept.
    void setNoiseOnly(bool on) noexcept { noiseOnly_ = on; }

    int latency() const noexcept { return latency_; }
    void process(const float* const* in, float* const* out, int numChannels, int numSamples) noexcept;

    // How far noise is being turned down now, dB (positive), for meters.
    double reductionNowDb() const noexcept;

    SpectralDenoise& spectral() noexcept { return spectral_; }
    const SpectralDenoise& spectral() const noexcept { return spectral_; }
    NeuralDenoise& voice() noexcept { return voice_; }
    const NeuralDenoise& voice() const noexcept { return voice_; }

private:
    struct Delay {
        std::vector<float> buf;
        int pos = 0;
        void setLength(int n);
        void clear();
        // writes x[i] and returns the sample from `length` ago, in place
        void process(float* x, int n) noexcept;
    };

    void switchTo(Mode m) noexcept;

    std::atomic<int> pending_ { 0 };
    SpectralDenoise spectral_;
    NeuralDenoise voice_;
    Mode mode_ = Mode::AnySound, fadingFrom_ = Mode::AnySound;
    int fadeLen_ = 0, fadePos_ = 0; // fadePos_ < fadeLen_: crossfading
    bool noiseOnly_ = false;
    int nch_ = 1, latency_ = 0, maxBlock_ = 0;
    std::vector<Delay> padSpectral_, padVoice_, dry_;
    std::vector<std::vector<float>> a_, b_, d_; // per-channel scratch
    std::vector<const float*> inPtr_;
    std::vector<float*> aPtr_, bPtr_;
};

} // namespace mend
