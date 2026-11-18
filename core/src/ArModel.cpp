// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#include "mend/ArModel.hpp"
#include "mend/Biquad.hpp" // kPi

#include <algorithm>
#include <cmath>

namespace mend {

void ArModel::prepare(int maxOrder, int maxGap, int maxFitLength)
{
    maxOrder_ = maxOrder;
    maxGap_ = maxGap;
    a_.assign(maxOrder + 1, 0.0);
    a_[0] = 1.0;
    order_ = 0;
    tmp_.assign(maxOrder + 1, 0.0);
    fwdErr_.assign(maxFitLength, 0.0);
    bwdErr_.assign(maxFitLength, 0.0);
    r_.assign(maxOrder + 1, 0.0);
    band_.assign(static_cast<size_t>(maxGap) * (maxOrder + 1), 0.0);
    b_.assign(maxGap, 0.0);
    e0_.assign(maxGap + maxOrder, 0.0);
    xs_.assign(maxGap, 0.0);
    orig_.assign(maxGap, 0.0);
}

bool ArModel::fit(const double* x, int n, int order)
{
    // Burg's method: minimises forward and backward prediction error
    // together, with no windowing bias. Much better than the windowed
    // autocorrelation method for interpolating short gaps in tonal audio.
    order = std::min(order, maxOrder_);
    n = std::min(n, static_cast<int>(fwdErr_.size()));

    double energy = 0.0;
    for (int i = 0; i < n; ++i) {
        fwdErr_[i] = x[i];
        bwdErr_[i] = x[i];
        energy += x[i] * x[i];
    }
    std::fill(a_.begin(), a_.end(), 0.0);
    a_[0] = 1.0;
    if (energy <= 1e-20 || n <= order + 1) {
        order_ = 0;
        return false;
    }

    for (int m = 1; m <= order; ++m) {
        double num = 0.0, den = 0.0;
        for (int i = m; i < n; ++i) {
            num += fwdErr_[i] * bwdErr_[i - 1];
            den += fwdErr_[i] * fwdErr_[i] + bwdErr_[i - 1] * bwdErr_[i - 1];
        }
        if (den <= 1e-30)
            break;
        const double k = -2.0 * num / den;

        for (int j = 0; j <= m; ++j)
            tmp_[j] = a_[j];
        for (int j = 1; j <= m; ++j)
            a_[j] = tmp_[j] + k * tmp_[m - j];

        // update errors from the top down so bwdErr_[i - 1] is still old
        for (int i = n - 1; i >= m; --i) {
            const double f = fwdErr_[i], b = bwdErr_[i - 1];
            fwdErr_[i] = f + k * b;
            bwdErr_[i] = b + k * f;
        }
    }
    order_ = order;
    return true;
}

// Solve T x = b where T is the symmetric banded Toeplitz matrix with
// first row r_[0..p] (zero beyond the band). Banded Cholesky: O(m p^2),
// and numerically stable even when the AR model has poles very near the
// unit circle (pure tones), where Levinson recursion breaks down.
bool ArModel::solveBanded(int m, int p)
{
    const int w = p + 1;
    auto L = [&](int i, int j) -> double& { return band_[static_cast<size_t>(i) * w + (i - j)]; };
    for (int i = 0; i < m; ++i) {
        for (int j = std::max(0, i - p); j <= i; ++j) {
            double s = r_[i - j];
            for (int k = std::max(0, i - p); k < j; ++k)
                s -= L(i, k) * L(j, k);
            if (i == j) {
                if (s <= 0.0)
                    return false;
                L(i, i) = std::sqrt(s);
            } else {
                L(i, j) = s / L(j, j);
            }
        }
    }
    // forward substitution: L y = b
    for (int i = 0; i < m; ++i) {
        double s = b_[i];
        for (int k = std::max(0, i - p); k < i; ++k)
            s -= L(i, k) * xs_[k];
        xs_[i] = s / L(i, i);
    }
    // back substitution: L^T x = y
    for (int i = m - 1; i >= 0; --i) {
        double s = xs_[i];
        for (int k = i + 1; k <= std::min(m - 1, i + p); ++k)
            s -= L(k, i) * xs_[k];
        xs_[i] = s / L(i, i);
    }
    return true;
}

void ArModel::interpolate(double* x, int g0, int g1)
{
    const int m = g1 - g0;
    const int p = order_;
    if (m <= 0 || m > maxGap_ || p == 0)
        return;

    // Prediction error with the unknown samples set to zero.
    for (int i = g0; i < g1; ++i) {
        orig_[i - g0] = x[i];
        x[i] = 0.0;
    }
    for (int t = g0; t < g1 + p; ++t)
        e0_[t - g0] = residual(x, t);

    // Normal equations: Toeplitz matrix from the autocorrelation of a[].
    for (int d = 0; d <= p; ++d) {
        double s = 0.0;
        for (int k = 0; k + d <= p; ++k)
            s += a_[k] * a_[k + d];
        r_[d] = s;
    }

    for (int i = 0; i < m; ++i) {
        double s = 0.0;
        for (int k = 0; k <= p; ++k)
            s += a_[k] * e0_[i + k];
        b_[i] = -s;
    }

    if (!solveBanded(m, p)) {
        for (int i = 0; i < m; ++i) // numerically hopeless: leave it untouched
            x[g0 + i] = orig_[i];
        return;
    }
    for (int i = 0; i < m; ++i)
        x[g0 + i] = xs_[i];
}

} // namespace mend
