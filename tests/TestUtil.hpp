// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
// Tiny shared helpers for the self-contained DSP tests.
#pragma once

#include "mend/Biquad.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

namespace mendtest {

inline int& failures()
{
    static int f = 0;
    return f;
}

#define CHECK(cond, ...)                                       \
    do {                                                       \
        if (!(cond)) {                                         \
            ++mendtest::failures();                            \
            std::printf("  FAIL %s:%d: ", __FILE__, __LINE__); \
            std::printf(__VA_ARGS__);                          \
            std::printf("\n");                                 \
        }                                                      \
    } while (0)

inline int finish()
{
    if (failures()) {
        std::printf("\n%d check(s) FAILED\n", failures());
        return 1;
    }
    std::printf("\nall checks passed\n");
    return 0;
}

// Run a mono processor with process(in, out, nch, n) over a signal in blocks.
template <typename P>
std::vector<float> runMono(P& proc, const std::vector<float>& input, int block)
{
    std::vector<float> out(input.size());
    for (size_t pos = 0; pos < input.size(); pos += block) {
        const int n = static_cast<int>(std::min<size_t>(block, input.size() - pos));
        const float* in[1] = { input.data() + pos };
        float* o[1] = { out.data() + pos };
        proc.process(in, o, 1, n);
    }
    return out;
}

// RMS difference between a[from..to) and b[from+shift..to+shift)
inline double rmsDiff(const std::vector<float>& a, const std::vector<float>& b, size_t from, size_t to, size_t shift = 0)
{
    double s = 0.0;
    for (size_t i = from; i < to; ++i) {
        const double d = static_cast<double>(a[i]) - b[i + shift];
        s += d * d;
    }
    return std::sqrt(s / static_cast<double>(to - from));
}

inline double rms(const std::vector<float>& a, size_t from, size_t to)
{
    double s = 0.0;
    for (size_t i = from; i < to; ++i)
        s += static_cast<double>(a[i]) * a[i];
    return std::sqrt(s / static_cast<double>(to - from));
}

// A music-like test signal: a few steady partials with slow vibrato
// plus a little noise.
inline std::vector<float> musicLike(double fs, double seconds, unsigned seed, double amp = 0.3)
{
    const size_t n = static_cast<size_t>(fs * seconds);
    std::vector<float> x(n);
    std::mt19937 rng(seed);
    std::normal_distribution<double> noise(0.0, 0.002);
    const double freqs[] = { 220.0, 330.0, 440.0, 660.0, 1320.0 };
    const double amps[] = { 1.0, 0.6, 0.5, 0.3, 0.15 };
    double ph[5] = {};
    for (size_t i = 0; i < n; ++i) {
        const double t = i / fs;
        double v = 0.0;
        for (int k = 0; k < 5; ++k) {
            const double f = freqs[k] * (1.0 + 0.003 * std::sin(2.0 * mend::kPi * 5.0 * t));
            ph[k] += 2.0 * mend::kPi * f / fs;
            v += amps[k] * std::sin(ph[k]);
        }
        x[i] = static_cast<float>(amp * v / 2.55 + noise(rng));
    }
    return x;
}

} // namespace mendtest
