// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
// Tests for the editor's one-shot modules (mend::offline).
#include "TestUtil.hpp"

#include "mend/Offline.hpp"

using namespace mend;
using namespace mend::offline;
using namespace mendtest;

namespace {
constexpr double kFs = 48000.0;

std::vector<float> sine(double hz, double amp, double seconds, double fs = kFs, double phase = 0.0)
{
    std::vector<float> x(static_cast<size_t>(fs * seconds));
    for (size_t i = 0; i < x.size(); ++i)
        x[i] = static_cast<float>(amp * std::sin(2 * kPi * hz * i / fs + phase));
    return x;
}

double toneAmp(const std::vector<float>& x, double hz, size_t from, size_t to, double fs = kFs)
{
    double re = 0, im = 0;
    for (size_t i = from; i < to; ++i) {
        re += x[i] * std::cos(2 * kPi * hz * i / fs);
        im -= x[i] * std::sin(2 * kPi * hz * i / fs);
    }
    return 2.0 * std::hypot(re, im) / static_cast<double>(to - from);
}
double db(double v) { return 20.0 * std::log10(v + 1e-20); }
} // namespace

static void testLevelAndFade()
{
    std::printf("Gain, normalize and fades\n");
    Channels x { sine(440, 0.25, 0.5), sine(440, 0.5, 0.5) };
    const double g = normalizePeak(x, -1.0, false);
    CHECK(std::abs(gainToDb(samplePeak(x)) + 1.0) < 0.01 && std::abs(g - (-1.0 - gainToDb(0.5))) < 0.01,
          "normalize peak %.3f dB", gainToDb(samplePeak(x)));
    normalizePeak(x, -3.0, true);
    CHECK(std::abs(gainToDb(samplePeak({ x[0] })) + 3.0) < 0.01, "per-channel normalize");
    for (FadeCurve c : { FadeCurve::Linear, FadeCurve::EqualPower, FadeCurve::SCurve, FadeCurve::Exponential,
                         FadeCurve::Logarithmic }) {
        Channels f { std::vector<float>(1000, 1.0f) };
        applyFade(f, true, c);
        CHECK(f[0][0] == 0.0f && std::abs(f[0][999] - 1.0f) < 1e-6 && f[0][500] > 0.0f && f[0][500] < 1.0f,
              "fade curve %d", static_cast<int>(c));
    }
}

static void testPhase()
{
    std::printf("Phase rotation, DC and polarity\n");
    Channels x { sine(1000, 0.5, 1.0) };
    rotatePhase(x, 90.0);
    const auto ref = sine(1000, 0.5, 1.0, kFs, kPi / 2); // sin rotated 90 deg -> cos
    double err = 0;
    for (size_t i = 10000; i < 38000; ++i)
        err = std::max(err, static_cast<double>(std::abs(x[0][i] - ref[i])));
    CHECK(err < 0.005, "90-degree rotation error %.4f", err);
    // an asymmetric wave: rotation should lower its peak
    Channels a { std::vector<float>(48000) }; // pulse-like: harmonics all in cosine phase
    for (size_t i = 0; i < a[0].size(); ++i) {
        const double t = 2 * kPi * 200.0 * i / kFs;
        double v = 0;
        for (int k = 1; k <= 10; ++k)
            v += std::cos(k * t) / k;
        a[0][i] = static_cast<float>(0.1 * v);
    }
    const double before = samplePeak(a);
    const double best = bestPhaseRotation(a);
    rotatePhase(a, best);
    Channels mid { std::vector<float>(a[0].begin() + 4096, a[0].end() - 4096) }; // away from the edges
    CHECK(samplePeak(mid) < before * 0.8, "best rotation (%.0f deg) peak %.3f vs %.3f", best, samplePeak(mid), before);
    Channels d { std::vector<float>(100, 0.25f) };
    removeDc(d);
    CHECK(std::abs(d[0][50]) < 1e-6, "DC not removed");
    invertPolarity(d, -1);
}

static void testAzimuth()
{
    std::printf("Azimuth: finds and corrects a delayed, quieter right channel\n");
    std::mt19937 rng(4);
    std::normal_distribution<double> nd(0, 0.1);
    std::vector<float> L(96000);
    Biquad lp1, lp2; // band-limited, like real audio
    lp1.setCoeffs(BiquadCoeffs::lowpass(kFs, 15000, 0.54));
    lp2.setCoeffs(BiquadCoeffs::lowpass(kFs, 15000, 1.31));
    for (auto& v : L)
        v = static_cast<float>(lp2.process(lp1.process(nd(rng))));
    // R = L delayed 3.4 samples, 2 dB down (band-limited fractional delay)
    Channels x { L, L };
    correctAzimuth(x, -3.4, -2.0);
    const Azimuth a = measureAzimuth(x, kFs);
    CHECK(std::abs(a.delaySamples - 3.4) < 0.15 && std::abs(a.gainDb - 2.0) < 0.1, "measured %.2f samples, %.2f dB",
          a.delaySamples, a.gainDb);
    correctAzimuth(x, a.delaySamples, a.gainDb);
    double e = 0, s = 0;
    for (size_t i = 1000; i < 95000; ++i) {
        e += (x[0][i] - x[1][i]) * (x[0][i] - x[1][i]);
        s += x[0][i] * x[0][i];
    }
    CHECK(10 * std::log10(e / s) < -25.0, "residual after correction %.1f dB", 10 * std::log10(e / s));
}

static void testInterpolate()
{
    std::printf("Interpolate rebuilds a 200-sample gap\n");
    Channels x { sine(700, 0.4, 0.2) };
    for (size_t c = 0; c < x[0].size(); ++c)
        x[0][c] += static_cast<float>(0.2 * std::sin(2 * kPi * 1900.0 * c / kFs));
    const auto orig = x[0];
    for (size_t i = 4000; i < 4200; ++i)
        x[0][i] = 0.0f;
    CHECK(interpolateGap(x, 4000, 200), "interpolation refused");
    const double err = rmsDiff(x[0], orig, 4000, 4200) / rms(orig, 4000, 4200);
    CHECK(err < 0.02, "gap error %.4f", err);
}

static void testLoudness()
{
    std::printf("Loudness (BS.1770) and true peak\n");
    // a -20 dBFS 997 Hz sine in one channel reads -23.0 LUFS
    Channels m { sine(997, 0.1, 10.0) };
    const Loudness l = measureLoudness(m, kFs);
    CHECK(std::abs(l.integrated + 23.01) < 0.1, "integrated %.2f LUFS", l.integrated);
    CHECK(l.range < 0.5, "range %.2f LU for a steady tone", l.range);
    // fs/4 sine at 45 degrees: samples at 0.707, true peak 1.0
    Channels q { sine(12000, 1.0, 0.5, kFs, kPi / 4) };
    CHECK(std::abs(truePeakDb(q, kFs)) < 0.2 && gainToDb(samplePeak(q)) < -2.9, "true peak %.2f dB",
          truePeakDb(q, kFs));
    Channels loud { sine(200, 2.0, 1.0) };
    limitPeaks(loud, kFs, -1.0);
    CHECK(truePeakDb(loud, kFs) < -0.85, "limited to %.2f dBTP", truePeakDb(loud, kFs));
}

static void testResampleAndTrim()
{
    std::printf("Resample 48 -> 44.1 kHz, trim silence\n");
    Channels x { sine(1000, 0.5, 1.0) };
    const Channels y = resample(x, 48000, 44100);
    CHECK(y[0].size() == 44100, "length %zu", y[0].size());
    const double a = toneAmp(y[0], 1000, 4410, 39690, 44100);
    CHECK(std::abs(db(a / 0.5)) < 0.05, "amplitude changed %.3f dB", db(a / 0.5));
    Channels up = resample(x, 48000, 96000);
    CHECK(std::abs(db(toneAmp(up[0], 1000, 9600, 86400, 96000) / 0.5)) < 0.05, "upsampled amplitude");

    Channels t { sine(500, 0.5, 1.0) };
    t[0].insert(t[0].end(), static_cast<size_t>(kFs * 2), 0.0f);
    const auto tail = sine(500, 0.5, 1.0);
    t[0].insert(t[0].end(), tail.begin(), tail.end());
    size_t removed = 0;
    const Channels tt = trimSilence(t, kFs, -60.0, 0.5, 0.3, &removed);
    CHECK(std::abs(static_cast<double>(removed) / kFs - 1.7) < 0.03 && tt[0].size() == t[0].size() - removed,
          "removed %.3f s", removed / kFs);
}

static void testEqMatchAndDither()
{
    std::printf("EQ Match pulls a dull recording towards a bright reference; dither\n");
    std::mt19937 rng(8);
    std::normal_distribution<double> nd(0, 0.1);
    Channels ref { std::vector<float>(96000) };
    for (auto& v : ref[0])
        v = static_cast<float>(nd(rng));
    Channels dull = ref;
    Biquad lp;
    lp.setCoeffs(BiquadCoeffs::lowpass(kFs, 2000, 0.707));
    for (auto& v : dull[0])
        v = static_cast<float>(lp.process(v));
    const int n = 4096;
    double binHz = 0;
    const auto r = averageSpectrumDb(ref, kFs, n, &binHz);
    const auto before = averageSpectrumDb(dull, kFs, n);
    eqMatch(dull, kFs, r, n, 1.0, 24.0);
    const auto after = averageSpectrumDb(dull, kFs, n);
    const int k = static_cast<int>(8000 / binHz);
    CHECK(r[k] - before[k] > 15.0 && std::abs(r[k] - after[k]) < 2.0, "8 kHz: %.1f dB away before, %.1f after",
          r[k] - before[k], r[k] - after[k]);

    Channels d { sine(440, 0.3, 0.2) };
    ditherTo(d, 16, true);
    bool onGrid = true;
    for (float v : d[0])
        onGrid &= std::abs(v * 32768.0 - std::round(v * 32768.0)) < 1e-3;
    CHECK(onGrid, "dithered samples not on the 16-bit grid");
}

static void testSpectral()
{
    std::printf("Spectral blend and spectral repair\n");
    // blend: processed differs everywhere, keep it only in 1..2 kHz
    Channels orig { sine(500, 0.3, 1.0) }, proc = orig;
    for (size_t i = 0; i < proc[0].size(); ++i)
        proc[0][i] += static_cast<float>(0.1 * std::sin(2 * kPi * 1500 * i / kFs) + 0.1 * std::sin(2 * kPi * 6000 * i / kFs));
    spectralBlend(proc, orig, kFs, 1000, 2000);
    CHECK(std::abs(toneAmp(proc[0], 1500, 4800, 43200) - 0.1) < 0.005, "in-band change lost");
    CHECK(toneAmp(proc[0], 6000, 4800, 43200) < 0.002, "out-of-band change kept (%.4f)", toneAmp(proc[0], 6000, 4800, 43200));
    CHECK(std::abs(toneAmp(proc[0], 500, 4800, 43200) - 0.3) < 0.003, "original disturbed");

    // repair: a 3 kHz whistle for 0.3 s over a steady 500 Hz tone
    Channels x { sine(500, 0.3, 2.0) };
    const size_t s = static_cast<size_t>(kFs * 0.85), len = static_cast<size_t>(kFs * 0.3);
    for (size_t i = s; i < s + len; ++i)
        x[0][i] += static_cast<float>(0.3 * std::sin(2 * kPi * 3000 * i / kFs));
    for (SpectralMode m : { SpectralMode::Attenuate, SpectralMode::Replace }) {
        Channels y = x;
        spectralRepair(y, kFs, s, len, 2500, 3500, m, 1.0, 0.0);
        const double whistle = db(toneAmp(y[0], 3000, s + 4800, s + len - 4800) / 0.3);
        const double tone = db(toneAmp(y[0], 500, s, s + len) / 0.3);
        CHECK(whistle < -25.0, "mode %d: whistle only %.1f dB down", static_cast<int>(m), whistle);
        CHECK(std::abs(tone) < 0.2, "mode %d: 500 Hz changed %.2f dB", static_cast<int>(m), tone);
    }
    Channels g { sine(3000, 0.3, 2.0) };
    spectralRepair(g, kFs, s, len, 2500, 3500, SpectralMode::Gain, 1.0, -12.0);
    CHECK(std::abs(db(toneAmp(g[0], 3000, s + 4800, s + len - 4800) / 0.3) + 12.0) < 0.5, "gain mode");
}

int main()
{
    testLevelAndFade();
    testPhase();
    testAzimuth();
    testInterpolate();
    testLoudness();
    testResampleAndTrim();
    testEqMatchAndDither();
    testSpectral();
    return finish();
}
