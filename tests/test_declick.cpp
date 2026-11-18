// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#include "TestUtil.hpp"
#include "mend/ArModel.hpp"
#include "mend/DeClick.hpp"

using namespace mend;
using namespace mendtest;

static void testArInterpolation()
{
    std::printf("AR interpolation fills a 40-sample hole in a music-like signal\n");
    const double fs = 48000.0;
    auto x = musicLike(fs, 0.1, 3);
    std::vector<double> d(x.begin(), x.end());
    const std::vector<double> truth = d;
    ArModel ar;
    ar.prepare(64, 128, 4096);
    const int g0 = 2000, g1 = 2040;
    for (int i = g0; i < g1; ++i)
        d[i] = 0.9; // obliterate
    ar.fit(d.data(), 1800, 64); // fit on clean audio before the hole
    ar.interpolate(d.data(), g0, g1);
    double err = 0.0, sig = 0.0;
    for (int i = g0; i < g1; ++i) {
        err += (d[i] - truth[i]) * (d[i] - truth[i]);
        sig += truth[i] * truth[i];
    }
    const double snr = 10.0 * std::log10(sig / err);
    std::printf("  reconstruction SNR %.1f dB\n", snr);
    CHECK(snr > 20.0, "reconstruction SNR only %.1f dB", snr);
}

struct Clicks {
    std::vector<float> clean, damaged;
    std::vector<std::pair<size_t, size_t>> where; // [start, end)
};

static Clicks makeClicks(double fs, double seconds, int count, unsigned seed)
{
    Clicks c;
    c.clean = musicLike(fs, seconds, seed);
    c.damaged = c.clean;
    std::mt19937 rng(seed + 100);
    std::uniform_real_distribution<double> pos(0.2, 0.95), amp(0.15, 0.6);
    std::uniform_int_distribution<int> width(1, static_cast<int>(fs * 0.0004)); // up to 0.4 ms
    std::bernoulli_distribution sign(0.5);
    for (int k = 0; k < count; ++k) {
        const size_t s = static_cast<size_t>(pos(rng) * c.clean.size());
        const int w = width(rng);
        const double a = amp(rng) * (sign(rng) ? 1 : -1);
        for (int i = 0; i < w; ++i) // decaying impulse burst
            c.damaged[s + i] += static_cast<float>(a * std::exp(-2.0 * i / w) * (i % 2 ? -0.5 : 1.0));
        c.where.push_back({ s, s + w });
    }
    return c;
}

static void testRemovesClicks(double fs)
{
    std::printf("De-click repairs 40 clicks at %.0f Hz\n", fs);
    auto c = makeClicks(fs, 4.0, 40, 11);
    DeClick dc;
    dc.prepare(fs, 1);
    dc.setSettings(DeClick::Settings{});
    auto out = runMono(dc, c.damaged, 512);
    const size_t L = dc.latency();

    double before = 0.0, after = 0.0;
    int fixed = 0;
    for (auto [s, e] : c.where) {
        const size_t from = s - 4, to = e + 8;
        double b = 0.0, a = 0.0;
        for (size_t i = from; i < to; ++i) {
            b += std::pow(c.damaged[i] - c.clean[i], 2);
            a += std::pow(out[i + L] - c.clean[i], 2);
        }
        before += b;
        after += a;
        if (a < 0.05 * b)
            ++fixed;
    }
    const double improvement = 10.0 * std::log10(before / std::max(after, 1e-30));
    std::printf("  click energy reduced by %.1f dB; %d/40 clicks repaired to <5%% residual; %ld repairs\n",
                improvement, fixed, dc.totalClicks());
    CHECK(improvement > 20.0, "only %.1f dB reduction", improvement);
    CHECK(fixed >= 36, "only %d/40 clicks repaired well", fixed);
}

static void testLeavesCleanAudioAlone()
{
    std::printf("De-click leaves clean audio alone (no false detections)\n");
    const double fs = 48000.0;
    auto x = musicLike(fs, 4.0, 21);
    DeClick dc;
    dc.prepare(fs, 1);
    dc.setSettings(DeClick::Settings{});
    auto out = runMono(dc, x, 256);
    const size_t L = dc.latency();
    const double err = rmsDiff(x, out, 0, x.size() - L, L);
    std::printf("  %ld repairs on clean audio, rms change %.2e\n", dc.totalClicks(), err);
    CHECK(dc.totalClicks() <= 2, "%ld false detections", dc.totalClicks());
    CHECK(err < 1e-4, "clean audio changed by %.2e rms", err);
}

static void testKeepsTransients()
{
    std::printf("De-click does not treat a drum-like transient as a click\n");
    const double fs = 48000.0;
    auto x = musicLike(fs, 2.0, 5, 0.1);
    // 30 ms decaying noise burst with sharp attack
    std::mt19937 rng(9);
    std::normal_distribution<double> n(0.0, 0.4);
    const size_t s = static_cast<size_t>(fs);
    for (size_t i = 0; i < static_cast<size_t>(fs * 0.03); ++i)
        x[s + i] += static_cast<float>(n(rng) * std::exp(-static_cast<double>(i) / (fs * 0.008)));
    DeClick dc;
    dc.prepare(fs, 1);
    dc.setSettings(DeClick::Settings{});
    auto out = runMono(dc, x, 512);
    const size_t L = dc.latency();
    const double change = rmsDiff(x, out, s, s + static_cast<size_t>(fs * 0.03), L);
    const double level = rms(x, s, s + static_cast<size_t>(fs * 0.03));
    std::printf("  transient changed by %.1f%% rms\n", 100.0 * change / level);
    CHECK(change < 0.1 * level, "transient changed by %.1f%%", 100.0 * change / level);
}

static void testClicksOnlyIsComplement()
{
    std::printf("Clicks-only output equals input minus repaired output\n");
    const double fs = 44100.0;
    auto c = makeClicks(fs, 2.0, 10, 4);
    DeClick a, b;
    a.prepare(fs, 1);
    b.prepare(fs, 1);
    DeClick::Settings s;
    a.setSettings(s);
    s.clicksOnly = true;
    b.setSettings(s);
    auto rep = runMono(a, c.damaged, 333);
    auto removed = runMono(b, c.damaged, 333);
    const size_t L = a.latency();
    double maxErr = 0.0;
    for (size_t i = 0; i + L < c.damaged.size(); ++i)
        maxErr = std::max(maxErr, std::abs(static_cast<double>(c.damaged[i]) - rep[i + L] - removed[i + L]));
    std::printf("  max error %.2e\n", maxErr);
    CHECK(maxErr < 1e-6, "complement error %.2e", maxErr);
}

int main()
{
    testArInterpolation();
    testRemovesClicks(48000.0);
    testRemovesClicks(96000.0);
    testLeavesCleanAudioAlone();
    testKeepsTransients();
    testClicksOnlyIsComplement();
    return finish();
}
