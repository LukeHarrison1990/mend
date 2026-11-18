// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#include "mend/SpectralTools.hpp"

#include "mend/Biquad.hpp"

#include <algorithm>
#include <cmath>

namespace mend {

int stftSizeFor(double sampleRate, double seconds)
{
    int n = 256;
    while (n < 32768 && n * 1.414 < sampleRate * seconds)
        n *= 2;
    return n;
}

// ---- DeReverb ------------------------------------------------------------------

void DeReverb::prepare(double sampleRate, int numChannels)
{
    fs_ = sampleRate;
    const int n = stftSizeFor(sampleRate, 0.021); // 1024 at 48 kHz: sharp enough in time for onsets
    stft_.prepare(n, 4, numChannels);
    nb_ = stft_.numBins();
    // late reverb starts ~50 ms after the direct sound
    delayFrames_ = std::max(1, static_cast<int>(std::lround(0.06 * sampleRate / stft_.hopSize())));
    ch_.assign(static_cast<size_t>(numChannels), {});
    for (auto& c : ch_) {
        c.psd.assign(static_cast<size_t>(nb_), 0.0);
        c.gain.assign(static_cast<size_t>(nb_), 1.0);
        c.history.assign(static_cast<size_t>(delayFrames_), std::vector<double>(static_cast<size_t>(nb_), 0.0));
        c.histPos = 0;
    }
}

void DeReverb::reset()
{
    stft_.reset();
    for (auto& c : ch_) {
        std::fill(c.psd.begin(), c.psd.end(), 0.0);
        std::fill(c.gain.begin(), c.gain.end(), 1.0);
        for (auto& h : c.history)
            std::fill(h.begin(), h.end(), 0.0);
        c.histPos = 0;
    }
}

void DeReverb::processFrame(std::complex<double>* bins, int numBins, int channel)
{
    Ch& c = ch_[static_cast<size_t>(channel)];
    const double hopSec = stft_.hopSize() / fs_;
    const double t60 = std::clamp(settings_.reverbTime, 0.05, 10.0);
    const double delta = 3.0 * std::log(10.0) / t60;
    const double decay = std::exp(-2.0 * delta * delayFrames_ * hopSec); // power decay over the delay
    const double beta = std::clamp(settings_.amount, 0.0, 100.0) / 100.0 * 1.5;
    const double floorGain = dbToGain(-std::max(0.0, settings_.maxReductionDb));
    const double alpha = 0.6; // PSD smoothing
    std::vector<double>& old = c.history[static_cast<size_t>(c.histPos)]; // PSD from delayFrames ago

    for (int k = 0; k < numBins; ++k) {
        const double p = std::norm(bins[k]);
        double& s = c.psd[static_cast<size_t>(k)];
        s = alpha * s + (1.0 - alpha) * p;
        const double late = decay * old[static_cast<size_t>(k)];
        double g = 1.0 - beta * late / (s + 1e-20);
        g = std::clamp(g, floorGain, 1.0);
        // fast to cut, slower to recover: avoids pumping on decays
        double& gs = c.gain[static_cast<size_t>(k)];
        gs = g < gs ? 0.5 * gs + 0.5 * g : 0.8 * gs + 0.2 * g;
    }
    // light smoothing across frequency, then apply
    double prev = c.gain[0];
    for (int k = 0; k < numBins; ++k) {
        const double next = c.gain[static_cast<size_t>(std::min(k + 1, numBins - 1))];
        const double g = 0.25 * prev + 0.5 * c.gain[static_cast<size_t>(k)] + 0.25 * next;
        prev = c.gain[static_cast<size_t>(k)];
        bins[k] *= g;
    }
    // remember this frame's smoothed PSD for delayFrames from now
    old = c.psd;
    c.histPos = (c.histPos + 1) % delayFrames_;
}

// ---- DeWind ----------------------------------------------------------------------

void DeWind::prepare(double sampleRate, int numChannels)
{
    fs_ = sampleRate;
    stft_.prepare(stftSizeFor(sampleRate, 0.043), 4, numChannels);
    power_.assign(static_cast<size_t>(stft_.numBins()), 0.0);
    gain_.assign(static_cast<size_t>(stft_.numBins()), 1.0);
}

void DeWind::reset()
{
    stft_.reset();
    std::fill(gain_.begin(), gain_.end(), 1.0);
}

void DeWind::processFrames(std::complex<double>* const* bins, int numBins, int numChannels)
{
    const double binHz = fs_ / stft_.fftSize();
    const double fc = std::clamp(settings_.frequency, 40.0, 2000.0);
    const int kc = std::max(2, static_cast<int>(fc / binHz));
    const int kRef1 = std::min(numBins - 1, kc * 3); // reference band: fc .. 3 fc
    for (int k = 0; k < numBins; ++k) {
        double p = 0.0;
        for (int c = 0; c < numChannels; ++c)
            p += std::norm(bins[c][k]);
        power_[static_cast<size_t>(k)] = p / numChannels;
    }
    double ref = 0.0;
    for (int k = kc; k <= kRef1; ++k)
        ref += power_[static_cast<size_t>(k)];
    ref /= std::max(1, kRef1 - kc + 1);
    const double strength = std::clamp(settings_.strength, 0.0, 100.0) / 100.0;
    const double floorGain = dbToGain(-std::max(0.0, settings_.maxReductionDb));
    // allowance: reference level, rising 3 dB/octave below the cutoff, plus
    // headroom (0..12 dB) that shrinks as strength rises
    const double headroom = dbToGain(12.0 - 12.0 * strength) * dbToGain(12.0 - 12.0 * strength); // power ratio
    for (int k = 0; k < numBins; ++k) {
        double g = 1.0;
        if (k < kc) {
            const double f = std::max(binHz * 0.5, k * binHz);
            const double allow = ref * (fc / f) * headroom;
            const double p = power_[static_cast<size_t>(k)];
            if (p > allow)
                g = std::max(floorGain, std::sqrt(allow / p));
        }
        double& gs = gain_[static_cast<size_t>(k)];
        gs = g < gs ? 0.3 * gs + 0.7 * g : 0.85 * gs + 0.15 * g; // fast attack, ~100 ms release
        for (int c = 0; c < numChannels; ++c)
            bins[c][k] *= gs;
    }
}

// ---- CenterExtract ----------------------------------------------------------------

void CenterExtract::prepare(double sampleRate, int numChannels)
{
    stft_.prepare(stftSizeFor(sampleRate, 0.043), 4, numChannels);
    sim_.assign(static_cast<size_t>(stft_.numBins()), 0.0);
}

void CenterExtract::reset()
{
    stft_.reset();
    std::fill(sim_.begin(), sim_.end(), 0.0);
}

void CenterExtract::processFrames(std::complex<double>* const* bins, int numBins, int numChannels)
{
    if (numChannels < 2)
        return; // nothing to compare: pass through (with the same latency)
    const double power = 1.0 + 7.0 * std::clamp(settings_.strength, 0.0, 100.0) / 100.0;
    const double wet = std::clamp(settings_.amount, 0.0, 100.0) / 100.0;
    std::complex<double>* L = bins[0];
    std::complex<double>* R = bins[1];
    for (int k = 0; k < numBins; ++k) {
        const double el = std::norm(L[k]), er = std::norm(R[k]);
        const double s = std::clamp(2.0 * std::real(L[k] * std::conj(R[k])) / (el + er + 1e-24), 0.0, 1.0);
        double& sm = sim_[static_cast<size_t>(k)];
        sm = 0.6 * sm + 0.4 * s;
        const double m = std::pow(sm, power);
        const std::complex<double> C = m * 0.5 * (L[k] + R[k]);
        std::complex<double> l, r;
        if (settings_.removeCenter) {
            l = L[k] - C;
            r = R[k] - C;
        } else {
            l = r = C;
        }
        L[k] = (1.0 - wet) * L[k] + wet * l;
        R[k] = (1.0 - wet) * R[k] + wet * r;
    }
}

} // namespace mend
