// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#pragma once

#include <cmath>
#include <complex>

namespace mend {

constexpr double kPi = 3.14159265358979323846;

// Normalised biquad coefficients (a0 == 1). Double precision because the
// filters in this suite sit very low in the spectrum (50 Hz at 192 kHz),
// where single-precision coefficients lose accuracy.
struct BiquadCoeffs {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;

    // RBJ cookbook peaking EQ. gainLinear < 1 cuts, > 1 boosts.
    static BiquadCoeffs peaking(double sampleRate, double freq, double q, double gainLinear) noexcept
    {
        const double A = std::sqrt(gainLinear);
        const double w0 = 2.0 * kPi * freq / sampleRate;
        const double alpha = std::sin(w0) / (2.0 * q);
        const double cw = std::cos(w0);
        const double a0 = 1.0 + alpha / A;
        BiquadCoeffs c;
        c.b0 = (1.0 + alpha * A) / a0;
        c.b1 = (-2.0 * cw) / a0;
        c.b2 = (1.0 - alpha * A) / a0;
        c.a1 = (-2.0 * cw) / a0;
        c.a2 = (1.0 - alpha / A) / a0;
        return c;
    }

    // Notch with finite depth: H = 1 - r * BP, where BP is a unity-peak
    // band-pass. Unlike a peaking cut, the -3 dB width stays freq / q no
    // matter how deep the cut is, so neighbouring material is untouched.
    // depthLinear is the gain at the centre (0 = infinite notch, 1 = no-op).
    static BiquadCoeffs notch(double sampleRate, double freq, double q, double depthLinear) noexcept
    {
        const double w0 = 2.0 * kPi * freq / sampleRate;
        const double alpha = std::sin(w0) / (2.0 * q);
        const double cw = std::cos(w0);
        const double a0 = 1.0 + alpha;
        const double r = 1.0 - depthLinear;
        BiquadCoeffs c;
        c.a1 = (-2.0 * cw) / a0;
        c.a2 = (1.0 - alpha) / a0;
        const double bp0 = alpha / a0, bp2 = -alpha / a0;
        c.b0 = 1.0 - r * bp0;
        c.b1 = c.a1;
        c.b2 = c.a2 - r * bp2;
        return c;
    }

    // RBJ cookbook low-pass.
    static BiquadCoeffs lowpass(double sampleRate, double freq, double q) noexcept
    {
        const double w0 = 2.0 * kPi * freq / sampleRate;
        const double alpha = std::sin(w0) / (2.0 * q);
        const double cw = std::cos(w0);
        const double a0 = 1.0 + alpha;
        BiquadCoeffs c;
        c.b0 = ((1.0 - cw) * 0.5) / a0;
        c.b1 = (1.0 - cw) / a0;
        c.b2 = c.b0;
        c.a1 = (-2.0 * cw) / a0;
        c.a2 = (1.0 - alpha) / a0;
        return c;
    }

    // RBJ cookbook high-pass.
    static BiquadCoeffs highpass(double sampleRate, double freq, double q) noexcept
    {
        const double w0 = 2.0 * kPi * freq / sampleRate;
        const double alpha = std::sin(w0) / (2.0 * q);
        const double cw = std::cos(w0);
        const double a0 = 1.0 + alpha;
        BiquadCoeffs c;
        c.b0 = ((1.0 + cw) * 0.5) / a0;
        c.b1 = -(1.0 + cw) / a0;
        c.b2 = c.b0;
        c.a1 = (-2.0 * cw) / a0;
        c.a2 = (1.0 - alpha) / a0;
        return c;
    }

    // RBJ cookbook shelves (slope S = 1). gainLinear < 1 cuts.
    static BiquadCoeffs lowShelf(double sampleRate, double freq, double gainLinear) noexcept
    {
        const double A = std::sqrt(gainLinear);
        const double w0 = 2.0 * kPi * freq / sampleRate;
        const double cw = std::cos(w0), alpha = std::sin(w0) / 2.0 * std::sqrt(2.0);
        const double sa = 2.0 * std::sqrt(A) * alpha;
        const double a0 = (A + 1) + (A - 1) * cw + sa;
        BiquadCoeffs c;
        c.b0 = A * ((A + 1) - (A - 1) * cw + sa) / a0;
        c.b1 = 2 * A * ((A - 1) - (A + 1) * cw) / a0;
        c.b2 = A * ((A + 1) - (A - 1) * cw - sa) / a0;
        c.a1 = -2 * ((A - 1) + (A + 1) * cw) / a0;
        c.a2 = ((A + 1) + (A - 1) * cw - sa) / a0;
        return c;
    }
    static BiquadCoeffs highShelf(double sampleRate, double freq, double gainLinear, double q = 0.70710678) noexcept
    {
        const double A = std::sqrt(gainLinear);
        const double w0 = 2.0 * kPi * freq / sampleRate;
        const double cw = std::cos(w0), alpha = std::sin(w0) / (2.0 * q);
        const double sa = 2.0 * std::sqrt(A) * alpha;
        const double a0 = (A + 1) - (A - 1) * cw + sa;
        BiquadCoeffs c;
        c.b0 = A * ((A + 1) + (A - 1) * cw + sa) / a0;
        c.b1 = -2 * A * ((A - 1) + (A + 1) * cw) / a0;
        c.b2 = A * ((A + 1) + (A - 1) * cw - sa) / a0;
        c.a1 = 2 * ((A - 1) - (A + 1) * cw) / a0;
        c.a2 = ((A + 1) - (A - 1) * cw - sa) / a0;
        return c;
    }

    // ITU-R BS.1770 K-weighting, both stages, for any sample rate (the
    // analogue prototype behind the standard's 48 kHz coefficients, as
    // used by libebur128).
    static void kWeighting(double sampleRate, BiquadCoeffs& shelf, BiquadCoeffs& highpass) noexcept
    {
        double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        double K = std::tan(kPi * f0 / sampleRate);
        const double Vh = std::pow(10.0, G / 20.0), Vb = std::pow(Vh, 0.4996667741545416);
        double a0 = 1.0 + K / Q + K * K;
        shelf.b0 = (Vh + Vb * K / Q + K * K) / a0;
        shelf.b1 = 2.0 * (K * K - Vh) / a0;
        shelf.b2 = (Vh - Vb * K / Q + K * K) / a0;
        shelf.a1 = 2.0 * (K * K - 1.0) / a0;
        shelf.a2 = (1.0 - K / Q + K * K) / a0;
        f0 = 38.13547087602444;
        Q = 0.5003270373238773;
        K = std::tan(kPi * f0 / sampleRate);
        a0 = 1.0 + K / Q + K * K;
        highpass.b0 = 1.0;
        highpass.b1 = -2.0;
        highpass.b2 = 1.0;
        highpass.a1 = 2.0 * (K * K - 1.0) / a0;
        highpass.a2 = (1.0 - K / Q + K * K) / a0;
    }

    // Complex frequency response at `freq`, used by UIs to draw curves.
    std::complex<double> response(double sampleRate, double freq) const noexcept
    {
        const double w = 2.0 * kPi * freq / sampleRate;
        const std::complex<double> z1 = std::polar(1.0, -w);
        const std::complex<double> z2 = z1 * z1;
        return (b0 + b1 * z1 + b2 * z2) / (1.0 + a1 * z1 + a2 * z2);
    }
};

// Transposed direct form II biquad: good numerical behaviour and
// tolerant of coefficient changes between blocks.
class Biquad {
public:
    void setCoeffs(const BiquadCoeffs& c) noexcept { c_ = c; }
    const BiquadCoeffs& coeffs() const noexcept { return c_; }
    void reset() noexcept { s1_ = s2_ = 0.0; }

    inline double process(double x) noexcept
    {
        const double y = c_.b0 * x + s1_;
        s1_ = c_.b1 * x - c_.a1 * y + s2_;
        s2_ = c_.b2 * x - c_.a2 * y;
        return y;
    }

private:
    BiquadCoeffs c_;
    double s1_ = 0.0, s2_ = 0.0;
};

inline double dbToGain(double db) noexcept { return std::pow(10.0, db / 20.0); }
inline double gainToDb(double g) noexcept { return 20.0 * std::log10(g > 1e-12 ? g : 1e-12); }

} // namespace mend
