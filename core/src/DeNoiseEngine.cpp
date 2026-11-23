// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#include "mend/DeNoiseEngine.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace mend {

void DeNoiseEngine::Delay::setLength(int n)
{
    buf.assign(static_cast<std::size_t>(std::max(n, 0)), 0.0f);
    pos = 0;
}

void DeNoiseEngine::Delay::clear()
{
    std::fill(buf.begin(), buf.end(), 0.0f);
    pos = 0;
}

void DeNoiseEngine::Delay::process(float* x, int n) noexcept
{
    const int len = static_cast<int>(buf.size());
    if (len == 0)
        return;
    for (int i = 0; i < n; ++i) {
        const float old = buf[static_cast<std::size_t>(pos)];
        buf[static_cast<std::size_t>(pos)] = x[i];
        x[i] = old;
        if (++pos == len)
            pos = 0;
    }
}

void DeNoiseEngine::prepare(double sampleRate, int numChannels, int maxBlock)
{
    nch_ = std::max(1, numChannels);
    maxBlock_ = std::max(1, maxBlock);
    spectral_.prepare(sampleRate, nch_);
    voice_.prepare(sampleRate, nch_, maxBlock_);
    const int ls = spectral_.latency(), lv = voice_.latency();
    latency_ = std::max(ls, lv);
    padSpectral_.assign(static_cast<std::size_t>(nch_), Delay {});
    padVoice_.assign(static_cast<std::size_t>(nch_), Delay {});
    dry_.assign(static_cast<std::size_t>(nch_), Delay {});
    for (int c = 0; c < nch_; ++c) {
        padSpectral_[static_cast<std::size_t>(c)].setLength(latency_ - ls);
        padVoice_[static_cast<std::size_t>(c)].setLength(latency_ - lv);
        dry_[static_cast<std::size_t>(c)].setLength(latency_);
    }
    a_.assign(static_cast<std::size_t>(nch_), std::vector<float>(static_cast<std::size_t>(maxBlock_)));
    b_.assign(static_cast<std::size_t>(nch_), std::vector<float>(static_cast<std::size_t>(maxBlock_)));
    d_.assign(static_cast<std::size_t>(nch_), std::vector<float>(static_cast<std::size_t>(maxBlock_)));
    inPtr_.resize(static_cast<std::size_t>(nch_));
    aPtr_.resize(static_cast<std::size_t>(nch_));
    bPtr_.resize(static_cast<std::size_t>(nch_));
    for (int c = 0; c < nch_; ++c) {
        aPtr_[static_cast<std::size_t>(c)] = a_[static_cast<std::size_t>(c)].data();
        bPtr_[static_cast<std::size_t>(c)] = b_[static_cast<std::size_t>(c)].data();
    }
    fadeLen_ = std::max(1, static_cast<int>(0.04 * sampleRate));
    fadePos_ = fadeLen_;
}

void DeNoiseEngine::reset()
{
    spectral_.reset();
    voice_.reset();
    for (auto* v : { &padSpectral_, &padVoice_, &dry_ })
        for (auto& d : *v)
            d.clear();
    fadePos_ = fadeLen_;
}

void DeNoiseEngine::switchTo(Mode m) noexcept
{
    if (m == mode_)
        return;
    // Bring the incoming engine in fresh (its state is from whenever it
    // last ran) and crossfade from the outgoing one.
    if (m == Mode::Voice)
        voice_.reset();
    else
        spectral_.reset();
    for (auto& d : (m == Mode::Voice ? padVoice_ : padSpectral_))
        d.clear();
    fadingFrom_ = mode_;
    mode_ = m;
    fadePos_ = 0;
}

void DeNoiseEngine::setSpectralSettings(const SpectralDenoise::Settings& s) noexcept
{
    SpectralDenoise::Settings t = s;
    t.noiseOnly = false;
    spectral_.setSettings(t);
}

void DeNoiseEngine::setVoiceReductionDb(float db) noexcept
{
    voice_.setReductionDb(db >= 59.95f ? 100.0f : std::max(db, 0.0f));
}

double DeNoiseEngine::reductionNowDb() const noexcept
{
    if (mode_ == Mode::AnySound)
        return spectral_.reductionAppliedDb();
    const float* g = voice_.bandGains();
    double s = 0.0;
    for (int b = 0; b < DeepFilterNet::kErb; ++b)
        s += static_cast<double>(g[b]) * g[b];
    s /= DeepFilterNet::kErb;
    return std::clamp(-10.0 * std::log10(std::max(s, 1e-6)), 0.0, 60.0);
}

void DeNoiseEngine::process(const float* const* in, float* const* out, int numChannels, int numSamples) noexcept
{
    const Mode wanted = static_cast<Mode>(pending_.load(std::memory_order_relaxed));
    if (wanted != mode_)
        switchTo(wanted);
    // in and out may be the same buffers; work in blocks no longer than maxBlock_
    for (int done = 0; done < numSamples;) {
        const int n = std::min(maxBlock_, numSamples - done);
        const int nch = std::min(numChannels, nch_);
        for (int c = 0; c < nch_; ++c) {
            const float* src = in[std::min(c, nch - 1)] + done;
            std::memcpy(d_[static_cast<std::size_t>(c)].data(), src, sizeof(float) * static_cast<std::size_t>(n));
            inPtr_[static_cast<std::size_t>(c)] = d_[static_cast<std::size_t>(c)].data();
        }
        const bool fading = fadePos_ < fadeLen_;
        auto run = [&](Mode m, std::vector<float*>& dst) {
            if (m == Mode::Voice) {
                voice_.process(inPtr_.data(), dst.data(), nch_, n);
                for (int c = 0; c < nch_; ++c)
                    padVoice_[static_cast<std::size_t>(c)].process(dst[static_cast<std::size_t>(c)], n);
            } else {
                spectral_.process(inPtr_.data(), dst.data(), nch_, n);
                for (int c = 0; c < nch_; ++c)
                    padSpectral_[static_cast<std::size_t>(c)].process(dst[static_cast<std::size_t>(c)], n);
            }
        };
        run(mode_, aPtr_);
        if (fading) {
            run(fadingFrom_, bPtr_);
            for (int c = 0; c < nch_; ++c) {
                float* a = aPtr_[static_cast<std::size_t>(c)];
                const float* b = bPtr_[static_cast<std::size_t>(c)];
                for (int i = 0; i < n; ++i) {
                    const float t = std::min(1.0f, static_cast<float>(fadePos_ + i) / static_cast<float>(fadeLen_));
                    a[i] = b[i] + (a[i] - b[i]) * t;
                }
            }
            fadePos_ = std::min(fadeLen_, fadePos_ + n);
        }
        // the dry signal, delayed to match (in place in d_)
        for (int c = 0; c < nch_; ++c)
            dry_[static_cast<std::size_t>(c)].process(d_[static_cast<std::size_t>(c)].data(), n);
        for (int c = 0; c < numChannels; ++c) {
            const int sc = std::min(c, nch_ - 1);
            const float* a = aPtr_[static_cast<std::size_t>(sc)];
            float* o = out[c] + done;
            if (noiseOnly_) {
                const float* dry = d_[static_cast<std::size_t>(sc)].data();
                for (int i = 0; i < n; ++i)
                    o[i] = dry[i] - a[i];
            } else {
                std::memcpy(o, a, sizeof(float) * static_cast<std::size_t>(n));
            }
        }
        done += n;
    }
}

} // namespace mend
