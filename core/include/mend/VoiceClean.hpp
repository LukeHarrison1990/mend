// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
//
// Voice: a one-stop speech polisher for podcasts, voice-over and video.
//
// A fixed chain of processors, steered by three macro controls and a
// microphone profile:
//
//   high-pass -> levelling -> microphone + character EQ -> de-mud (dynamic
//   low-mid cut) -> clarity (multiband transient emphasis) -> resonance
//   smoothing (adaptive, STFT) -> compression and gentle expansion ->
//   de-ess -> output gain -> peak limiter
//
//   Clarity  brings consonants forward: attacks in the 0.7-3.5 kHz and
//            3.5 kHz+ bands are emphasised, plus a little presence.
//   Smooth   evens out the voice: levelling, compression, expansion of
//            the gaps, and how hard resonances are smoothed.
//   De-mud   cuts low-mid build-up (boxy rooms, proximity effect) when,
//            and only as much as, it is there.
//
// A microphone profile corrects the typical colouration of one model of
// microphone and sets starting positions for the three controls. The
// profiles are approximations from each model's published character, not
// measurements of individual microphones.
#pragma once

#include "mend/Biquad.hpp"
#include "mend/Dynamics.hpp"
#include "mend/Stft.hpp"

#include <atomic>
#include <memory>
#include <vector>

namespace mend {

class VoiceClean {
public:
    struct Band {
        enum Type { None, LowShelf, Peak, HighShelf } type = None;
        double hz = 1000.0, db = 0.0, q = 1.0;
    };

    struct MicProfile {
        const char* name;
        const char* group; // "General", "Studio", "USB", ...
        double highPassHz;
        Band bands[4];
        float clarity, smooth, deMud; // suggested control positions
    };

    struct Character {
        const char* name;
        Band bands[4];
        double extraResonanceDb; // added to the resonance smoother's depth
        bool expander;           // close the gaps between phrases
    };

    static constexpr int kNumMics = 28;
    static constexpr int kNumCharacters = 5;
    static int numMics();
    static const MicProfile& mic(int index);
    static int numCharacters();
    static const Character& character(int index);

    struct Settings {
        int mic = 0;
        int character = 0;
        double clarity = 50.0; // 0..100
        double smooth = 50.0;  // 0..100
        double deMud = 50.0;   // 0..100
        double outputDb = 0.0;
    };

    // What the chain is doing right now (safe to read from another thread).
    struct Readout {
        std::atomic<float> levelDb { 0.0f };      // levelling gain
        std::atomic<float> deMudDb { 0.0f };      // low-mid cut (positive = cut)
        std::atomic<float> clarityDb { 0.0f };    // transient emphasis on consonants
        std::atomic<float> resonanceDb { 0.0f };  // deepest resonance cut
        std::atomic<float> compressDb { 0.0f };   // compression gain reduction
        std::atomic<float> deEssDb { 0.0f };      // sibilance reduction
        std::atomic<float> limitDb { 0.0f };      // limiter gain reduction
        std::atomic<float> outputPeakDb { -90.0f };
    };

    VoiceClean();
    ~VoiceClean();
    void prepare(double sampleRate, int numChannels);
    void reset();
    void setSettings(const Settings& s);
    const Settings& settings() const noexcept { return settings_; }
    int latency() const noexcept;
    void process(const float* const* in, float* const* out, int numChannels, int numSamples);

    const Readout& readout() const noexcept { return readout_; }

    // Response of the fixed EQ (high-pass, microphone, character, static
    // part of de-mud) in dB, for drawing.
    double staticResponseDb(double hz) const;

    // The resonance smoother's current cut per frequency, in dB (<= 0),
    // sampled at `hz` (for drawing).
    double resonanceCutDb(double hz) const;

private:
    struct ResonanceSmoother;

    static constexpr int kChunk = 2048;
    void processChunk(const float* const* in, float* const* out, int numChannels, int numSamples);
    void design();
    void addBands(const Band* bands, int n);

    Settings settings_;
    double fs_ = 48000.0;
    int nch_ = 1;

    // ---- fixed EQ -----------------------------------------------------------
    std::vector<BiquadCoeffs> eqCoeffs_;            // HP sections first
    std::vector<std::vector<Biquad>> eq_;           // [channel][section]

    // ---- levelling ----------------------------------------------------------
    double levelEnv_ = 0.0, levelGainDb_ = 0.0, levelCoef_ = 0.0;

    // ---- de-mud -------------------------------------------------------------
    std::vector<Biquad> mudFilter_;
    double mudCutDb_ = 0.0;
    int mudCounter_ = 0;

    // ---- clarity (3-band split, transient emphasis) --------------------------
    struct ClarityCh {
        Biquad lp1a, lp1b, hp1a, hp1b; // split at f1
        Biquad lp2a, lp2b, hp2a, hp2b; // split the upper part at f2
        Biquad apLa, apLb, apHa, apHb; // all-pass the low band at f2 (LP2 + HP2)
    };
    std::vector<ClarityCh> clar_;
    double fastEnv_[2] = {}, slowEnv_[2] = {};
    double fastAtt_ = 0, fastRel_ = 0, slowAtt_ = 0, slowRel_ = 0;
    double clarityGainSm_[2] = { 1.0, 1.0 };

    // ---- resonance smoothing ------------------------------------------------
    std::unique_ptr<ResonanceSmoother> res_;
    Stft stft_;

    // ---- compression / expansion ----------------------------------------------
    double compEnv_ = 0.0, compGainDb_ = 0.0, compAtt_ = 0, compRel_ = 0, compEnvCoef_ = 0;

    // ---- de-ess ---------------------------------------------------------------
    DeEss deEss_;

    // ---- limiter ----------------------------------------------------------------
    DelayLines limDelay_;
    std::vector<float> limPeaks_; // sliding window of the linked peak
    int limPos_ = 0, limLook_ = 64;
    double limGain_ = 1.0, limRel_ = 0.0;

    std::vector<std::vector<float>> bufA_, bufB_;
    Readout readout_;
    double peakHold_ = 0.0;
};

} // namespace mend
