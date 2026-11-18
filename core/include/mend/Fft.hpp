// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#pragma once

#include <complex>
#include <vector>

namespace mend {

// Radix-2 complex FFT with precomputed twiddles. Power-of-two sizes only.
// Allocation-free after prepare(); safe on the audio thread.
class Fft {
public:
    void prepare(int size);
    int size() const noexcept { return n_; }

    // In-place transforms. inverse() includes the 1/N scaling.
    void forward(std::complex<double>* data) const noexcept { transform(data, false); }
    void inverse(std::complex<double>* data) const noexcept;

    // Real input of length N -> N/2+1 bins. `scratch` must hold N values.
    void forwardReal(const double* in, std::complex<double>* bins, std::complex<double>* scratch) const noexcept;
    // N/2+1 bins (Hermitian) -> real output of length N.
    void inverseReal(const std::complex<double>* bins, double* out, std::complex<double>* scratch) const noexcept;

private:
    void transform(std::complex<double>* data, bool inverse) const noexcept;

    int n_ = 0;
    std::vector<int> bitrev_;
    std::vector<std::complex<double>> twiddle_;
};

} // namespace mend
