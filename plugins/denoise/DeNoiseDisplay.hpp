// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
//
// Live spectrum shared from the De-noise DSP to its UI (same process).
// The DSP publishes with a sequence lock; the UI copies only consistent
// snapshots and never blocks the audio thread.
#pragma once

#include <array>
#include <atomic>
#include <mutex>
#include <utility>
#include <vector>

struct DeNoiseDisplay {
    static constexpr int kMaxBins = 16385; // FFT up to 32768

    std::atomic<unsigned> seq { 0 };
    int bins = 0;
    int fftSize = 0;
    double sampleRate = 48000.0;
    std::array<float, kMaxBins> in {}, out {}, noise {};

    // audio thread
    template <typename V>
    void publish(const V& i, const V& o, const V& n, int numBins, int fft, double fs)
    {
        seq.fetch_add(1, std::memory_order_acq_rel); // odd: writing
        bins = numBins < kMaxBins ? numBins : kMaxBins;
        fftSize = fft;
        sampleRate = fs;
        for (int k = 0; k < bins; ++k) {
            in[k] = i[k];
            out[k] = o[k];
            noise[k] = n[k];
        }
        seq.fetch_add(1, std::memory_order_release); // even: done
    }

    // UI thread: true if a consistent snapshot was copied
    bool read(std::vector<float>& i, std::vector<float>& o, std::vector<float>& n, int& numBins, int& fft, double& fs) const
    {
        const unsigned s1 = seq.load(std::memory_order_acquire);
        if (s1 & 1u)
            return false;
        numBins = bins;
        fft = fftSize;
        fs = sampleRate;
        i.assign(in.begin(), in.begin() + numBins);
        o.assign(out.begin(), out.begin() + numBins);
        n.assign(noise.begin(), noise.begin() + numBins);
        std::atomic_thread_fence(std::memory_order_acquire);
        return seq.load(std::memory_order_relaxed) == s1;
    }
};

// Maps a plugin instance (the pointer the UI gets from
// getPluginInstancePointer()) to its display data.
namespace denoise_registry {
inline std::mutex& mutex()
{
    static std::mutex m;
    return m;
}
inline std::vector<std::pair<const void*, DeNoiseDisplay*>>& entries()
{
    static std::vector<std::pair<const void*, DeNoiseDisplay*>> e;
    return e;
}
inline void add(const void* key, DeNoiseDisplay* d)
{
    std::lock_guard<std::mutex> l(mutex());
    entries().emplace_back(key, d);
}
inline void remove(const void* key)
{
    std::lock_guard<std::mutex> l(mutex());
    auto& e = entries();
    for (size_t i = 0; i < e.size(); ++i)
        if (e[i].first == key) {
            e.erase(e.begin() + static_cast<long>(i));
            return;
        }
}
inline DeNoiseDisplay* find(const void* key)
{
    std::lock_guard<std::mutex> l(mutex());
    for (auto& p : entries())
        if (p.first == key)
            return p.second;
    return nullptr;
}
} // namespace denoise_registry
