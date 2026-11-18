// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#include "mend/DeHum.hpp"

#include <algorithm>
#include <cmath>

namespace mend {

namespace {
constexpr int kSubBlock = 32; // coefficients are refreshed at most this often
}

void DeHum::prepare(double sampleRate, int maxBlockSize, int numChannels)
{
    sampleRate_ = sampleRate;
    numChannels_ = std::clamp(numChannels, 1, kMaxChannels);
    detector_.prepare(sampleRate);
    mono_.assign(std::max(maxBlockSize, kSubBlock), 0.0f);
    reset();
}

void DeHum::reset()
{
    detector_.reset();
    for (auto& ch : filters_)
        for (auto& f : ch)
            f.reset();
    currentFreq_ = settings_.frequency;
    designedFreq_ = -1.0;
    removedDb_ = -120.0;
    wasActive_ = false;
}

int DeHum::designCascade(double sampleRate, double f0, const Settings& s,
                         std::array<BiquadCoeffs, kMaxHarmonics>& out) noexcept
{
    const int wanted = std::clamp(s.harmonics, 1, kMaxHarmonics);
    const double gain = dbToGain(-std::clamp(s.reductionDb, 0.0, 80.0));
    const double q = std::clamp(s.q, 1.0, 500.0);
    int n = 0;
    for (int h = 1; h <= wanted; ++h) {
        const double f = f0 * h;
        if (f >= sampleRate * 0.45)
            break;
        out[n++] = BiquadCoeffs::notch(sampleRate, f, q, gain);
    }
    return n;
}

void DeHum::updateFilters(double f0)
{
    const Settings& s = settings_;
    const bool same = std::abs(f0 - designedFreq_) < 1e-4
        && s.harmonics == designedSettings_.harmonics
        && s.q == designedSettings_.q
        && s.reductionDb == designedSettings_.reductionDb;
    if (same)
        return;

    const int n = designCascade(sampleRate_, f0, s, coeffs_);
    for (int c = 0; c < numChannels_; ++c) {
        for (int k = 0; k < n; ++k)
            filters_[c][k].setCoeffs(coeffs_[k]);
        // Sections that were switched off start clean if re-enabled later.
        for (int k = n; k < activeSections_; ++k)
            filters_[c][k].reset();
    }
    activeSections_ = n;
    designedFreq_ = f0;
    designedSettings_ = s;
}

void DeHum::process(const float* const* in, float* const* out, int numChannels, int numSamples)
{
    numChannels = std::min(numChannels, numChannels_);
    double removedSq = 0.0;

    // Glide toward the target fundamental: ~0.3 s time constant, so that
    // tracking corrections never cause audible zipper noise.
    const double glide = 1.0 - std::exp(-static_cast<double>(kSubBlock) / (0.3 * sampleRate_));

    for (int start = 0; start < numSamples; start += kSubBlock) {
        const int n = std::min(kSubBlock, numSamples - start);

        if (settings_.autoDetect) {
            for (int i = 0; i < n; ++i) {
                float sum = 0.0f;
                for (int c = 0; c < numChannels; ++c)
                    sum += in[c][start + i];
                mono_[i] = sum / static_cast<float>(numChannels);
            }
            detector_.process(mono_.data(), n);
        }

        // Until a hum has been found, auto mode sits on the manual frequency.
        const auto& det = detector_.result();
        const double target = (settings_.autoDetect && det.everLocked) ? det.frequency : settings_.frequency;
        if (settings_.autoDetect && det.everLocked && !wasActive_) {
            currentFreq_ = target; // first lock: start right on the hum, no glide
            wasActive_ = true;
        }
        currentFreq_ += (target - currentFreq_) * glide;
        if (std::abs(target - currentFreq_) < 1e-4)
            currentFreq_ = target;
        updateFilters(currentFreq_);
        // In auto mode nothing is removed until hum has actually been found.
        const int sections = active() ? activeSections_ : 0;

        for (int c = 0; c < numChannels; ++c) {
            auto& chain = filters_[c];
            const float* src = in[c] + start;
            float* dst = out[c] + start;
            for (int i = 0; i < n; ++i) {
                const double x = src[i];
                double y = x;
                for (int k = 0; k < sections; ++k)
                    y = chain[k].process(y);
                const double removed = x - y;
                removedSq += removed * removed;
                dst[i] = static_cast<float>(settings_.humOnly ? removed : y);
            }
        }
    }

    const double denom = static_cast<double>(std::max(1, numSamples * numChannels));
    removedDb_ = gainToDb(std::sqrt(removedSq / denom));
}

} // namespace mend
