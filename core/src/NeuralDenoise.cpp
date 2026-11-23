// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#include "mend/NeuralDenoise.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>

namespace mend {

extern const std::size_t kDeepFilterNet3WeightCount;
extern const std::uint16_t kDeepFilterNet3Weights[];

namespace {

constexpr double kPi = 3.14159265358979323846;

float halfToFloat(std::uint16_t h) noexcept
{
    const std::uint32_t sign = static_cast<std::uint32_t>(h & 0x8000u) << 16;
    std::uint32_t exp = (h >> 10) & 0x1Fu;
    std::uint32_t man = h & 0x3FFu;
    std::uint32_t bits;
    if (exp == 0) {
        if (man == 0) {
            bits = sign;
        } else { // subnormal: normalise
            exp = 127 - 15 + 1;
            while ((man & 0x400u) == 0) {
                man <<= 1;
                --exp;
            }
            man &= 0x3FFu;
            bits = sign | (exp << 23) | (man << 13);
        }
    } else if (exp == 31) {
        bits = sign | 0x7F800000u | (man << 13);
    } else {
        bits = sign | ((exp - 15 + 127) << 23) | (man << 13);
    }
    float f;
    std::memcpy(&f, &bits, sizeof f);
    return f;
}

// The built-in weights, unpacked once and shared by every instance.
const std::vector<float>& weights()
{
    static std::vector<float> w;
    static std::once_flag once;
    std::call_once(once, [] {
        w.resize(kDeepFilterNet3WeightCount);
        for (std::size_t i = 0; i < kDeepFilterNet3WeightCount; ++i)
            w[i] = halfToFloat(kDeepFilterNet3Weights[i]);
    });
    return w;
}

double sinc(double x)
{
    return std::fabs(x) < 1e-12 ? 1.0 : std::sin(kPi * x) / (kPi * x);
}

double besselI0(double x)
{
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 50; ++k) {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
        if (term < 1e-12 * sum)
            break;
    }
    return sum;
}

} // namespace

// ---------------------------------------------------------------------------
// StreamResampler

void StreamResampler::prepare(double fsIn, double fsOut)
{
    bypass_ = std::fabs(fsIn - fsOut) < 1e-6;
    step_ = fsIn / fsOut;
    if (bypass_) {
        W_ = 0;
        table_.clear();
        reset();
        return;
    }
    const double fc = std::min(1.0, fsOut / fsIn) * 0.95; // cutoff, relative to the input Nyquist
    const int zeros = 24;
    W_ = static_cast<int>(std::ceil(zeros / fc));
    const double beta = 8.0, i0b = besselI0(beta);
    table_.assign(static_cast<std::size_t>(2 * W_ * sub_ + 2), 0.0f);
    for (std::size_t i = 0; i < table_.size(); ++i) {
        const double u = static_cast<double>(i) / sub_ - W_;
        const double r = u / W_;
        const double win = std::fabs(r) <= 1.0 ? besselI0(beta * std::sqrt(1.0 - r * r)) / i0b : 0.0;
        table_[i] = static_cast<float>(fc * sinc(fc * u) * win);
    }
    reset();
}

void StreamResampler::reset()
{
    buf_.reserve(65536);
    buf_.assign(static_cast<std::size_t>(W_), 0.0f); // zeros before the first sample
    pos_ = W_;
}

void StreamResampler::push(const float* in, int n)
{
    buf_.insert(buf_.end(), in, in + n);
}

int StreamResampler::available() const noexcept
{
    if (bypass_)
        return static_cast<int>(buf_.size());
    const double last = static_cast<double>(buf_.size()) - W_ - 1; // highest position with full support
    return last < pos_ ? 0 : static_cast<int>(std::floor((last - pos_) / step_)) + 1;
}

int StreamResampler::pull(float* out, int maxOut) noexcept
{
    int n = 0;
    if (bypass_) {
        n = std::min(maxOut, static_cast<int>(buf_.size()));
        std::copy(buf_.begin(), buf_.begin() + n, out);
        buf_.erase(buf_.begin(), buf_.begin() + n);
        return n;
    }
    const double limit = static_cast<double>(buf_.size()) - W_ - 1;
    while (n < maxOut && pos_ <= limit) {
        const long k0 = static_cast<long>(std::floor(pos_));
        const double frac = pos_ - k0;
        float v = 0.0f;
        for (long k = k0 - W_ + 1; k <= k0 + W_; ++k) {
            const double p = (frac + static_cast<double>(k0 - k) + W_) * sub_; // (pos - k + W) * sub
            const std::size_t i0 = static_cast<std::size_t>(p);
            const float f = static_cast<float>(p - static_cast<double>(i0));
            v += buf_[static_cast<std::size_t>(k)] * (table_[i0] + f * (table_[i0 + 1] - table_[i0]));
        }
        out[n++] = v;
        pos_ += step_;
    }
    // drop input no longer needed
    const long drop = static_cast<long>(std::floor(pos_)) - W_;
    if (drop > 4096) {
        buf_.erase(buf_.begin(), buf_.begin() + drop);
        pos_ -= static_cast<double>(drop);
    }
    return n;
}

// ---------------------------------------------------------------------------
// NeuralDenoise

NeuralDenoise::NeuralDenoise()
{
    const auto& w = weights();
    net_.setWeights(w.data(), w.size());
}

NeuralDenoise::~NeuralDenoise() = default;

bool NeuralDenoise::available()
{
    return kDeepFilterNet3WeightCount == DeepFilterNet::weightCount();
}

void NeuralDenoise::prepare(double sampleRate, int numChannels, int maxBlock)
{
    fs_ = sampleRate;
    nch_ = std::clamp(numChannels, 1, DeepFilterNet::kMaxChannels);
    net_.setChannels(nch_);
    up_.assign(static_cast<std::size_t>(nch_), StreamResampler {});
    down_.assign(static_cast<std::size_t>(nch_), StreamResampler {});
    for (int c = 0; c < nch_; ++c) {
        up_[static_cast<std::size_t>(c)].prepare(sampleRate, DeepFilterNet::kSampleRate);
        down_[static_cast<std::size_t>(c)].prepare(DeepFilterNet::kSampleRate, sampleRate);
    }
    const std::size_t big = static_cast<std::size_t>(std::max(maxBlock, 1)) * 4 + 8192;
    inFifo_.assign(static_cast<std::size_t>(nch_), {});
    outFifo_.assign(static_cast<std::size_t>(nch_), {});
    tmp_.assign(static_cast<std::size_t>(nch_), std::vector<float>(big));
    for (auto& v : inFifo_)
        v.reserve(big);
    for (auto& v : outFifo_)
        v.reserve(big);
    hopInBuf_.assign(static_cast<std::size_t>(nch_), std::vector<float>(DeepFilterNet::kHop));
    hopOutBuf_.assign(static_cast<std::size_t>(nch_), std::vector<float>(DeepFilterNet::kHop));
    hopIn_.resize(static_cast<std::size_t>(nch_));
    hopOut_.resize(static_cast<std::size_t>(nch_));
    for (int c = 0; c < nch_; ++c) {
        hopIn_[static_cast<std::size_t>(c)] = hopInBuf_[static_cast<std::size_t>(c)].data();
        hopOut_[static_cast<std::size_t>(c)] = hopOutBuf_[static_cast<std::size_t>(c)].data();
    }
    reset();
}

void NeuralDenoise::reset()
{
    net_.reset();
    net_.setAttenuationLimitDb(reductionDb_);
    for (auto& r : up_)
        r.reset();
    for (auto& r : down_)
        r.reset();
    // Prefill the processed side so a host block never waits for a hop:
    // one hop, plus the resamplers' look-ahead when the rate isn't 48 kHz.
    const StreamResampler& u = up_.front();
    const StreamResampler& d = down_.front();
    int prefill = DeepFilterNet::kHop;
    if (!u.bypass())
        prefill += d.delayIn() + static_cast<int>(std::ceil(u.delayIn() * DeepFilterNet::kSampleRate / fs_)) + 4;
    for (int c = 0; c < nch_; ++c) {
        inFifo_[static_cast<std::size_t>(c)].clear();
        outFifo_[static_cast<std::size_t>(c)].assign(static_cast<std::size_t>(prefill), 0.0f);
    }
    // In the 48 kHz stream, output index = input index + prefill + model latency;
    // the resamplers are centred, so they add no delay of their own.
    latency_ = static_cast<int>(std::lround((prefill + DeepFilterNet::latency()) * fs_ / DeepFilterNet::kSampleRate));
}

void NeuralDenoise::setReductionDb(float db) noexcept
{
    reductionDb_ = db;
    net_.setAttenuationLimitDb(db);
}

void NeuralDenoise::process(const float* const* in, float* const* out, int numChannels, int numSamples) noexcept
{
    const int nch = std::min(numChannels, nch_);
    constexpr int hop = DeepFilterNet::kHop;
    // 1. into the 48 kHz stream
    for (int c = 0; c < nch_; ++c) {
        const float* src = in[std::min(c, nch - 1)];
        auto& fifo = inFifo_[static_cast<std::size_t>(c)];
        StreamResampler& r = up_[static_cast<std::size_t>(c)];
        if (r.bypass()) {
            fifo.insert(fifo.end(), src, src + numSamples);
        } else {
            r.push(src, numSamples);
            auto& t = tmp_[static_cast<std::size_t>(c)];
            const int n = r.pull(t.data(), static_cast<int>(t.size()));
            fifo.insert(fifo.end(), t.data(), t.data() + n);
        }
    }
    // 2. whole hops through the network
    std::size_t done = 0;
    while (inFifo_.front().size() - done >= static_cast<std::size_t>(hop)) {
        for (int c = 0; c < nch_; ++c)
            std::memcpy(hopIn_[static_cast<std::size_t>(c)], inFifo_[static_cast<std::size_t>(c)].data() + done,
                        sizeof(float) * hop);
        net_.processHop(hopIn_.data(), hopOut_.data());
        for (int c = 0; c < nch_; ++c) {
            auto& o = outFifo_[static_cast<std::size_t>(c)];
            o.insert(o.end(), hopOut_[static_cast<std::size_t>(c)], hopOut_[static_cast<std::size_t>(c)] + hop);
        }
        done += hop;
    }
    if (done > 0)
        for (auto& f : inFifo_)
            f.erase(f.begin(), f.begin() + static_cast<std::ptrdiff_t>(done));
    // 3. back to the host rate
    for (int c = 0; c < nch_; ++c) {
        auto& o = outFifo_[static_cast<std::size_t>(c)];
        StreamResampler& r = down_[static_cast<std::size_t>(c)];
        float* dst = c < numChannels ? out[c] : tmp_[static_cast<std::size_t>(c)].data();
        int got;
        if (r.bypass()) {
            got = std::min(numSamples, static_cast<int>(o.size()));
            std::memcpy(dst, o.data(), sizeof(float) * static_cast<std::size_t>(got));
            o.erase(o.begin(), o.begin() + got);
        } else {
            r.push(o.data(), static_cast<int>(o.size()));
            o.clear();
            got = r.pull(dst, numSamples);
        }
        if (got < numSamples) // shouldn't happen: the prefill covers it
            std::fill(dst + got, dst + numSamples, 0.0f);
    }
    for (int c = nch_; c < numChannels; ++c)
        std::memcpy(out[c], out[nch_ - 1], sizeof(float) * static_cast<std::size_t>(numSamples));
}

} // namespace mend
