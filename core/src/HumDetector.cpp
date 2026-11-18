// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#include "mend/HumDetector.hpp"

#include <algorithm>
#include <cmath>

namespace mend {

namespace {
constexpr double kSearchHalfWidth = 2.0;   // Hz either side of 50 and 60
constexpr double kMinPeakRatio = 10.0;     // fundamental/2nd harmonic vs neighbours (10 dB)
constexpr double kNeighbourOffset = 1.5;   // Hz: where "off-peak" is measured
constexpr double kAgreeHz = 0.15;          // consecutive scans must agree this closely
constexpr int kScansToLock = 3;            // ~1.5 s of steady hum to lock
constexpr int kScansToUnlock = 4;
constexpr double kMinConfidence = 0.35;
constexpr double kMinEnergyFraction = 1e-6; // hum must hold >= -60 dB of the analysed energy
} // namespace

void HumDetector::prepare(double sampleRate)
{
    sampleRate_ = sampleRate;
    decimation_ = std::max(1, static_cast<int>(sampleRate / 3000.0));
    decimRate_ = sampleRate / decimation_;

    // Butterworth 4th order as two biquads (Q = 0.5412, 1.3066), cut-off
    // comfortably above the 4th harmonic of the highest candidate.
    const double maxF0 = 60.0 + kSearchHalfWidth;
    const double cutoff = std::min(maxF0 * kHarmonics * 1.6, decimRate_ * 0.4);
    aa1_.setCoeffs(BiquadCoeffs::lowpass(sampleRate, cutoff, 0.54119610));
    aa2_.setCoeffs(BiquadCoeffs::lowpass(sampleRate, cutoff, 1.30656296));

    // 2 seconds of history: bin width ~0.5 Hz, sharpened by harmonic summing.
    const int len = static_cast<int>(decimRate_ * 2.0);
    ring_.assign(len, 0.0f);
    snapshot_.assign(len, 0.0f);
    window_.resize(len);
    for (int i = 0; i < len; ++i)
        window_[i] = static_cast<float>(0.5 - 0.5 * std::cos(2.0 * kPi * i / (len - 1)));

    scanInterval_ = static_cast<int>(decimRate_ * 0.5); // rescan every 0.5 s

    candidates_.clear();
    for (double centre : { 50.0, 60.0 })
        for (double f = centre - kSearchHalfWidth; f <= centre + kSearchHalfWidth + 1e-9; f += kCoarseStep)
            candidates_.push_back(f);
    coarseEnergy_.assign(candidates_.size(), 0.0);
    sortScratch_.assign(candidates_.size(), 0.0);
    fineEnergy_.assign(2 * kFineHalfSpan + 1, 0.0);

    reset();
}

void HumDetector::reset()
{
    aa1_.reset();
    aa2_.reset();
    std::fill(ring_.begin(), ring_.end(), 0.0f);
    ringPos_ = 0;
    ringFilled_ = 0;
    decimCounter_ = 0;
    samplesSinceScan_ = 0;
    phase_ = Phase::Idle;
    lastFound_ = 0.0;
    agreeCount_ = missCount_ = 0;
    result_ = Result{};
}

void HumDetector::process(const float* mono, int numSamples)
{
    const int len = static_cast<int>(ring_.size());
    for (int i = 0; i < numSamples; ++i) {
        const double y = aa2_.process(aa1_.process(mono[i]));
        if (++decimCounter_ >= decimation_) {
            decimCounter_ = 0;
            ring_[ringPos_] = static_cast<float>(y);
            ringPos_ = (ringPos_ + 1) % len;
            if (ringFilled_ < len)
                ++ringFilled_;
            ++samplesSinceScan_;
        }
    }

    if (phase_ == Phase::Idle) {
        if (ringFilled_ == len && samplesSinceScan_ >= scanInterval_)
            beginScan();
    } else {
        stepScan(8); // ~8 candidates per call bounds the per-block cost
    }
}

void HumDetector::beginScan()
{
    samplesSinceScan_ = 0;
    const int len = static_cast<int>(ring_.size());
    for (int i = 0; i < len; ++i)
        snapshot_[i] = ring_[(ringPos_ + i) % len] * window_[i];
    phase_ = Phase::Coarse;
    candidateIndex_ = 0;
}

double HumDetector::goertzel(double f) const
{
    const double coeff = 2.0 * std::cos(2.0 * kPi * f / decimRate_);
    double s1 = 0.0, s2 = 0.0;
    for (float v : snapshot_) {
        const double s = v + coeff * s1 - s2;
        s2 = s1;
        s1 = s;
    }
    return s1 * s1 + s2 * s2 - coeff * s1 * s2;
}

double HumDetector::harmonicSum(double f0) const
{
    double total = 0.0;
    for (int h = 1; h <= kHarmonics; ++h) {
        if (f0 * h >= decimRate_ * 0.45)
            break;
        total += goertzel(f0 * h);
    }
    return total;
}

void HumDetector::stepScan(int maxCandidates)
{
    if (phase_ == Phase::Coarse) {
        const int count = static_cast<int>(candidates_.size());
        for (int k = 0; k < maxCandidates && candidateIndex_ < count; ++k, ++candidateIndex_)
            coarseEnergy_[candidateIndex_] = harmonicSum(candidates_[candidateIndex_]);
        if (candidateIndex_ < count)
            return;

        const auto best = std::max_element(coarseEnergy_.begin(), coarseEnergy_.end());
        fineCentre_ = candidates_[static_cast<size_t>(best - coarseEnergy_.begin())];

        // Confidence: how far the best candidate stands above the typical one.
        std::copy(coarseEnergy_.begin(), coarseEnergy_.end(), sortScratch_.begin());
        std::nth_element(sortScratch_.begin(), sortScratch_.begin() + count / 2, sortScratch_.end());
        const double median = sortScratch_[count / 2] + 1e-20;
        scanConfidence_ = std::clamp(std::log10(*best / median) / std::log10(40.0), 0.0, 1.0);

        phase_ = Phase::Fine;
        candidateIndex_ = 0;
        return;
    }

    if (phase_ == Phase::Fine) {
        const int count = static_cast<int>(fineEnergy_.size());
        for (int k = 0; k < maxCandidates && candidateIndex_ < count; ++k, ++candidateIndex_)
            fineEnergy_[candidateIndex_] = harmonicSum(fineCentre_ + (candidateIndex_ - kFineHalfSpan) * kFineStep);
        if (candidateIndex_ < count)
            return;

        const int bi = static_cast<int>(std::max_element(fineEnergy_.begin(), fineEnergy_.end()) - fineEnergy_.begin());
        double f = fineCentre_ + (bi - kFineHalfSpan) * kFineStep;
        if (bi > 0 && bi < count - 1) { // parabolic interpolation
            const double a = fineEnergy_[bi - 1], b = fineEnergy_[bi], c = fineEnergy_[bi + 1];
            const double denom = a - 2.0 * b + c;
            if (std::abs(denom) > 1e-30)
                f += 0.5 * (a - c) / denom * kFineStep;
        }

        // The hum must carry a real share of the signal. Without this, a pure
        // synthetic tone (nothing else in the spectrum) makes numerical
        // leakage at 50/60 Hz look like a clear peak.
        double energy = 0.0;
        for (float v : snapshot_)
            energy += static_cast<double>(v) * v;
        const double n = static_cast<double>(snapshot_.size());
        // Goertzel power of a Hann-windowed tone is n/3 times its windowed energy
        const double fraction = energy > 0.0 ? harmonicSum(f) * 3.0 / (n * energy) : 0.0;

        // Hum has a sharp peak at its fundamental or 2nd harmonic.
        double peakRatio = 0.0;
        if (fraction >= kMinEnergyFraction) {
            const double floor = energy * n / 3.0 * 1e-9; // ignore numerical-noise neighbours
            for (int h = 1; h <= 2; ++h) {
                const double on = goertzel(f * h);
                const double off = 0.5 * (goertzel(f * h - kNeighbourOffset) + goertzel(f * h + kNeighbourOffset));
                peakRatio = std::max(peakRatio, on / std::max(off, floor));
            }
        }
        finishScan(f, peakRatio);
        phase_ = Phase::Idle;
    }
}

void HumDetector::finishScan(double f, double peakRatio)
{
    const bool found = scanConfidence_ >= kMinConfidence && peakRatio >= kMinPeakRatio;
    if (found) {
        missCount_ = 0;
        agreeCount_ = (agreeCount_ > 0 && std::abs(f - lastFound_) <= kAgreeHz) ? agreeCount_ + 1 : 1;
        lastFound_ = f;
        if (agreeCount_ >= kScansToLock) {
            result_.locked = true;
            result_.everLocked = true;
            result_.frequency = f;
        }
    } else {
        agreeCount_ = 0;
        if (++missCount_ >= kScansToUnlock)
            result_.locked = false;
    }
    result_.confidence = result_.locked ? std::max(scanConfidence_, kMinConfidence) : std::min(scanConfidence_, kMinConfidence - 0.01);
}

} // namespace mend
