// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#include "TestUtil.hpp"
#include "mend/DeClip.hpp"

using namespace mend;
using namespace mendtest;

static std::vector<float> clipped(const std::vector<float>& x, float level)
{
    std::vector<float> y(x.size());
    for (size_t i = 0; i < x.size(); ++i)
        y[i] = std::clamp(x[i], -level, level);
    return y;
}

static void testRepairsClipping(bool autoThreshold, double clipFraction)
{
    std::printf("De-clip rebuilds peaks clipped at %.0f%% of peak (%s threshold)\n",
                clipFraction * 100.0, autoThreshold ? "auto" : "manual");
    const double fs = 48000.0;
    auto x = musicLike(fs, 3.0, 2, 0.9);
    float peak = 0.0f;
    for (float v : x)
        peak = std::max(peak, std::abs(v));
    const float level = static_cast<float>(peak * clipFraction);
    auto y = clipped(x, level);

    DeClip dc;
    dc.prepare(fs, 1);
    DeClip::Settings s;
    s.autoThreshold = autoThreshold;
    s.thresholdDb = gainToDb(level);
    dc.setSettings(s);
    auto out = runMono(dc, y, 512);
    const size_t L = dc.latency();

    // error only where clipping happened, after a second of settling
    double before = 0.0, after = 0.0;
    size_t count = 0;
    for (size_t i = static_cast<size_t>(fs); i + L < x.size(); ++i) {
        if (std::abs(y[i]) >= level * 0.999f) {
            before += std::pow(y[i] - x[i], 2);
            after += std::pow(out[i + L] - x[i], 2);
            ++count;
        }
    }
    const double improvement = 10.0 * std::log10(before / std::max(after, 1e-30));
    std::printf("  %zu clipped samples; error reduced by %.1f dB; clip level found %.2f dBFS (true %.2f)\n",
                count, improvement, dc.clipLevelDb(), gainToDb(level));
    CHECK(count > 100, "test signal should clip");
    CHECK(improvement > 10.0, "only %.1f dB better", improvement);
    if (autoThreshold)
        CHECK(std::abs(dc.clipLevelDb() - gainToDb(level)) < 0.1, "clip level estimate off");
}

static void testCleanAudioUntouched()
{
    std::printf("De-clip leaves unclipped audio alone (manual threshold above peak)\n");
    const double fs = 44100.0;
    auto x = musicLike(fs, 2.0, 8, 0.5);
    DeClip dc;
    dc.prepare(fs, 1);
    DeClip::Settings s;
    s.autoThreshold = false;
    s.thresholdDb = -0.1;
    dc.setSettings(s);
    auto out = runMono(dc, x, 300);
    const size_t L = dc.latency();
    const double err = rmsDiff(x, out, 0, x.size() - L, L);
    std::printf("  rms change %.2e, %ld samples repaired\n", err, dc.totalRepairedSamples());
    CHECK(err < 1e-7 && dc.totalRepairedSamples() == 0, "clean audio was changed");
}

int main()
{
    testRepairsClipping(false, 0.6);
    testRepairsClipping(true, 0.6);
    testRepairsClipping(true, 0.8);
    testCleanAudioUntouched();
    return finish();
}
