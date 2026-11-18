// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#pragma once

#include <vector>

namespace mend {

// Autoregressive (linear prediction) modelling and least-squares AR
// interpolation (Janssen, Veldhuis & Vries 1986; Godsill & Rayner 1998).
//
// This is the reconstruction engine behind De-click and De-clip: missing
// or damaged samples are replaced by the values that make the local
// prediction error as small as possible, so the repair continues the
// surrounding waveform's resonances naturally.
class ArModel {
public:
    // maxOrder: largest AR order; maxGap: longest gap that can be filled.
    void prepare(int maxOrder, int maxGap, int maxFitLength);

    // Fit coefficients a[0..order] (a[0] = 1) to x[0..n) with Burg's method.
    // Returns false if the segment is silent.
    bool fit(const double* x, int n, int order);

    int order() const noexcept { return order_; }
    const double* coeffs() const noexcept { return a_.data(); }

    // Prediction error of x at index t (requires t >= order).
    double residual(const double* x, int t) const noexcept
    {
        double e = 0.0;
        for (int k = 0; k <= order_; ++k)
            e += a_[k] * x[t - k];
        return e;
    }

    // Replace x[g0..g1) with least-squares AR interpolation.
    // Requires g0 >= order and g1 + order <= length of x.
    void interpolate(double* x, int g0, int g1);

private:
    bool solveBanded(int m, int p);

    int maxOrder_ = 0, maxGap_ = 0, order_ = 0;
    std::vector<double> a_, tmp_, fwdErr_, bwdErr_;
    // interpolation scratch
    std::vector<double> r_, b_, e0_, xs_, orig_, band_;
};

} // namespace mend
