// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#pragma once

#include "mend/Biquad.hpp"

#include <vector>

namespace mend {

// Finds the fundamental of mains hum. Searches near 50 Hz and 60 Hz
// (+/- 2 Hz, which covers mains drift and most tape speed error).
//
// The signal is low-passed and decimated to ~3 kHz, kept in a 2 s ring
// buffer, and periodically scanned with Goertzel filters summing the
// energy of the first harmonics for each candidate. Two safeguards stop
// music being mistaken for hum:
//   * the fundamental or 2nd harmonic itself must be a sharp spectral
//     peak (music whose partials merely line up with a candidate's upper
//     harmonics is rejected), and
//   * the same frequency must be found on several consecutive scans
//     (hum is constant; notes change).
//
// The scan is spread over many process() calls, so no single audio block
// pays for it. No allocation happens after prepare().
class HumDetector {
public:
    struct Result {
        double frequency = 50.0;  // latest locked estimate, Hz
        double confidence = 0.0;  // 0..1: how clearly hum is present
        bool locked = false;      // hum currently confirmed
        bool everLocked = false;  // hum has been confirmed since reset
    };

    void prepare(double sampleRate);
    void reset();

    // Feed mono audio. Cheap per sample; does a slice of the scan per call.
    void process(const float* mono, int numSamples);

    const Result& result() const noexcept { return result_; }

private:
    void beginScan();
    void stepScan(int maxCandidates);
    void finishScan(double f, double peakRatio);
    double harmonicSum(double f0) const;
    double goertzel(double f) const;

    double sampleRate_ = 48000.0;
    int decimation_ = 16;
    double decimRate_ = 3000.0;
    int decimCounter_ = 0;
    Biquad aa1_, aa2_; // anti-alias low-pass, 4th order Butterworth

    std::vector<float> ring_;
    int ringPos_ = 0;
    int ringFilled_ = 0;
    int samplesSinceScan_ = 0;
    int scanInterval_ = 1500;

    std::vector<float> window_;    // Hann window
    std::vector<float> snapshot_;  // windowed copy being analysed

    std::vector<double> candidates_; // coarse grid around 50 and 60 Hz
    std::vector<double> coarseEnergy_, sortScratch_, fineEnergy_;

    enum class Phase { Idle, Coarse, Fine };
    Phase phase_ = Phase::Idle;
    int candidateIndex_ = 0;
    double fineCentre_ = 50.0;
    double scanConfidence_ = 0.0;

    // consecutive-scan agreement
    double lastFound_ = 0.0;
    int agreeCount_ = 0;
    int missCount_ = 0;

    Result result_;

    static constexpr int kHarmonics = 4;
    static constexpr double kCoarseStep = 0.1;
    static constexpr double kFineStep = 0.01;
    static constexpr int kFineHalfSpan = 10; // +/- 0.1 Hz
};

} // namespace mend
