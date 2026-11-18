// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#include "mend/Eq.hpp"

#include <algorithm>
#include <cmath>

namespace mend {

bool ParametricEq::Settings::operator==(const Settings& o) const noexcept
{
    return highPassHz == o.highPassHz && lowShelfHz == o.lowShelfHz && lowShelfDb == o.lowShelfDb && peakHz == o.peakHz
        && peakDb == o.peakDb && peakQ == o.peakQ && highShelfHz == o.highShelfHz && highShelfDb == o.highShelfDb
        && lowPassHz == o.lowPassHz;
}

void ParametricEq::prepare(double sampleRate, int numChannels)
{
    sampleRate_ = sampleRate;
    numChannels_ = numChannels;
    filters_.assign(static_cast<size_t>(numChannels), {});
    design();
}

void ParametricEq::reset()
{
    for (auto& ch : filters_)
        for (auto& f : ch)
            f.reset();
}

void ParametricEq::setSettings(const Settings& s)
{
    if (s == settings_ && !active_.empty())
        return;
    settings_ = s;
    design();
}

void ParametricEq::design()
{
    const double nyq = sampleRate_ * 0.5;
    const auto fit = [nyq](double f) { return std::clamp(f, 10.0, nyq * 0.95); };
    std::vector<BiquadCoeffs> c;
    if (settings_.highPassHz > 20.0) { // Butterworth 4th order: Q 0.541 and 1.307
        c.push_back(BiquadCoeffs::highpass(sampleRate_, fit(settings_.highPassHz), 0.54119610));
        c.push_back(BiquadCoeffs::highpass(sampleRate_, fit(settings_.highPassHz), 1.30656296));
    }
    if (std::abs(settings_.lowShelfDb) > 0.01)
        c.push_back(BiquadCoeffs::lowShelf(sampleRate_, fit(settings_.lowShelfHz), dbToGain(settings_.lowShelfDb)));
    for (int i = 0; i < 3; ++i)
        if (std::abs(settings_.peakDb[i]) > 0.01)
            c.push_back(BiquadCoeffs::peaking(sampleRate_, fit(settings_.peakHz[i]), std::max(0.1, settings_.peakQ[i]),
                                              dbToGain(settings_.peakDb[i])));
    if (std::abs(settings_.highShelfDb) > 0.01)
        c.push_back(BiquadCoeffs::highShelf(sampleRate_, fit(settings_.highShelfHz), dbToGain(settings_.highShelfDb)));
    if (settings_.lowPassHz < 20000.0 && settings_.lowPassHz < nyq * 0.95) {
        c.push_back(BiquadCoeffs::lowpass(sampleRate_, fit(settings_.lowPassHz), 0.54119610));
        c.push_back(BiquadCoeffs::lowpass(sampleRate_, fit(settings_.lowPassHz), 1.30656296));
    }
    // Keep filter state for sections that persist, so moving a knob
    // doesn't click; sections that appear start from rest.
    const bool sameCount = c.size() == active_.size();
    active_ = c;
    for (auto& ch : filters_)
        for (size_t i = 0; i < active_.size(); ++i) {
            if (!sameCount)
                ch[i].reset();
            ch[i].setCoeffs(active_[i]);
        }
}

void ParametricEq::process(const float* const* in, float* const* out, int numChannels, int numSamples)
{
    const int nch = std::min(numChannels, numChannels_);
    const size_t ns = active_.size();
    for (int c = 0; c < nch; ++c) {
        auto& f = filters_[static_cast<size_t>(c)];
        for (int i = 0; i < numSamples; ++i) {
            double x = in[c][i];
            for (size_t k = 0; k < ns; ++k)
                x = f[k].process(x);
            out[c][i] = static_cast<float>(x);
        }
    }
}

double ParametricEq::responseDb(double hz) const
{
    std::complex<double> h(1.0, 0.0);
    for (const auto& c : active_)
        h *= c.response(sampleRate_, hz);
    return gainToDb(std::abs(h));
}

} // namespace mend
