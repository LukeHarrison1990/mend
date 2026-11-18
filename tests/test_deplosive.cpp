// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#include "TestUtil.hpp"
#include "mend/DePlosive.hpp"

using namespace mend;
using namespace mendtest;

// Voiced speech stand-in: 110 Hz glottal-like harmonic series with a
// formant emphasis, amplitude-modulated into syllables.
static std::vector<float> voice(double fs, double seconds)
{
    const size_t n = static_cast<size_t>(fs * seconds);
    std::vector<float> x(n);
    for (size_t i = 0; i < n; ++i) {
        const double t = i / fs;
        double v = 0.0;
        for (int h = 1; h <= 30; ++h) {
            const double f = 110.0 * h;
            const double formant = 1.0 + 2.0 * std::exp(-std::pow((f - 700.0) / 300.0, 2))
                                 + 1.5 * std::exp(-std::pow((f - 1800.0) / 400.0, 2));
            v += formant / h * std::sin(2.0 * kPi * f * t + h);
        }
        const double syllable = 0.5 + 0.5 * std::sin(2.0 * kPi * 3.0 * t);
        x[i] = static_cast<float>(0.05 * v * syllable);
    }
    return x;
}

// One-pole spectral split used only for measuring.
static std::vector<float> filtered(const std::vector<float>& x, double fs, double f, bool low)
{
    Biquad a, b;
    const auto c = low ? BiquadCoeffs::lowpass(fs, f, 0.7071) : BiquadCoeffs::highpass(fs, f, 0.7071);
    a.setCoeffs(c);
    b.setCoeffs(c);
    std::vector<float> y(x.size());
    for (size_t i = 0; i < x.size(); ++i)
        y[i] = static_cast<float>(b.process(a.process(x[i])));
    return y;
}

static void testTamesPlosives()
{
    std::printf("De-plosive reduces low-frequency pops, leaves the voice\n");
    const double fs = 48000.0;
    const auto clean = voice(fs, 4.0);
    auto x = clean;
    std::vector<size_t> pops;
    for (double t : { 0.7, 1.6, 2.5, 3.3 }) {
        const size_t s = static_cast<size_t>(t * fs);
        pops.push_back(s);
        for (size_t i = 0; i < static_cast<size_t>(fs * 0.12); ++i) {
            const double tt = i / fs;
            const double env = (1.0 - std::exp(-tt / 0.002)) * std::exp(-tt / 0.03);
            x[s + i] += static_cast<float>(0.7 * env * std::sin(2.0 * kPi * 70.0 * tt));
        }
    }

    DePlosive dp;
    dp.prepare(fs, 1);
    dp.setSettings(DePlosive::Settings{});
    auto out = runMono(dp, x, 256);
    const size_t L = dp.latency();
    std::vector<float> aligned(out.begin() + L, out.end());
    aligned.resize(x.size(), 0.0f);

    // How much pop is left: low band of (signal - clean voice), before and
    // after. The crossover is all-pass, so compare against the clean voice
    // passed through an identical De-plosive with nothing to detect.
    DePlosive ref;
    ref.prepare(fs, 1);
    DePlosive::Settings off;
    off.maxReductionDb = 0.0;
    ref.setSettings(off);
    auto cleanOut = runMono(ref, clean, 256);
    std::vector<float> popIn(x.size()), popOut(x.size());
    for (size_t i = 0; i < x.size(); ++i) {
        popIn[i] = x[i] - clean[i];
        popOut[i] = out[i] - cleanOut[i];
    }
    double worst = 1e9;
    for (size_t s : pops) {
        const size_t e = s + static_cast<size_t>(fs * 0.08);
        const double red = 20.0 * std::log10(rms(popIn, s, e) / rms(popOut, s + L, e + L));
        std::printf("  pop at %.2f s: pop energy reduced by %.1f dB\n", s / fs, red);
        worst = std::min(worst, red);
    }
    CHECK(worst > 8.0, "weakest pop reduction only %.1f dB", worst);

    // voice band away from the pops must be unchanged
    const auto inHigh = filtered(x, fs, 300.0, false), outHigh = filtered(aligned, fs, 300.0, false);
    const size_t a = static_cast<size_t>(0.1 * fs), b = static_cast<size_t>(0.6 * fs);
    const double change = 20.0 * std::log10(rms(outHigh, a, b) / rms(inHigh, a, b));
    std::printf("  voice band between pops changed by %+.2f dB\n", change);
    CHECK(std::abs(change) < 0.3, "voice band changed by %.2f dB", change);
}

static void testVoiceAloneUntouched()
{
    std::printf("De-plosive does not react to plain voiced speech\n");
    const double fs = 44100.0;
    auto x = voice(fs, 3.0);
    DePlosive dp;
    dp.prepare(fs, 1);
    dp.setSettings(DePlosive::Settings{});
    double maxGr = 0.0;
    std::vector<float> out(x.size());
    for (size_t pos = 0; pos < x.size(); pos += 512) {
        const int n = static_cast<int>(std::min<size_t>(512, x.size() - pos));
        const float* in[1] = { x.data() + pos };
        float* o[1] = { out.data() + pos };
        dp.process(in, o, 1, n);
        if (pos > fs * 0.2)
            maxGr = std::max(maxGr, dp.reductionDb());
    }
    std::printf("  max gain reduction on voice: %.2f dB\n", maxGr);
    CHECK(maxGr < 1.0, "reduced voice by %.2f dB", maxGr);
}

int main()
{
    testTamesPlosives();
    testVoiceAloneUntouched();
    return finish();
}
