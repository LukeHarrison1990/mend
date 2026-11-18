// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#include "mend/Fft.hpp"
#include "mend/Biquad.hpp" // kPi

#include <cassert>
#include <utility>

namespace mend {

void Fft::prepare(int size)
{
    assert(size >= 2 && (size & (size - 1)) == 0);
    n_ = size;
    bitrev_.resize(size);
    int bits = 0;
    while ((1 << bits) < size)
        ++bits;
    for (int i = 0; i < size; ++i) {
        int r = 0;
        for (int b = 0; b < bits; ++b)
            if (i & (1 << b))
                r |= 1 << (bits - 1 - b);
        bitrev_[i] = r;
    }
    twiddle_.resize(size / 2);
    for (int i = 0; i < size / 2; ++i)
        twiddle_[i] = std::polar(1.0, -2.0 * kPi * i / size);
}

void Fft::transform(std::complex<double>* d, bool inverse) const noexcept
{
    for (int i = 0; i < n_; ++i)
        if (i < bitrev_[i])
            std::swap(d[i], d[bitrev_[i]]);

    for (int len = 2; len <= n_; len <<= 1) {
        const int half = len >> 1;
        const int step = n_ / len;
        for (int start = 0; start < n_; start += len) {
            for (int k = 0; k < half; ++k) {
                std::complex<double> w = twiddle_[k * step];
                if (inverse)
                    w = std::conj(w);
                const std::complex<double> u = d[start + k];
                const std::complex<double> v = d[start + k + half] * w;
                d[start + k] = u + v;
                d[start + k + half] = u - v;
            }
        }
    }
}

void Fft::inverse(std::complex<double>* data) const noexcept
{
    transform(data, true);
    const double s = 1.0 / n_;
    for (int i = 0; i < n_; ++i)
        data[i] *= s;
}

void Fft::forwardReal(const double* in, std::complex<double>* bins, std::complex<double>* scratch) const noexcept
{
    for (int i = 0; i < n_; ++i)
        scratch[i] = { in[i], 0.0 };
    transform(scratch, false);
    for (int i = 0; i <= n_ / 2; ++i)
        bins[i] = scratch[i];
}

void Fft::inverseReal(const std::complex<double>* bins, double* out, std::complex<double>* scratch) const noexcept
{
    const int half = n_ / 2;
    for (int i = 0; i <= half; ++i)
        scratch[i] = bins[i];
    for (int i = half + 1; i < n_; ++i)
        scratch[i] = std::conj(bins[n_ - i]);
    inverse(scratch);
    for (int i = 0; i < n_; ++i)
        out[i] = scratch[i].real();
}

} // namespace mend
