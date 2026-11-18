// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
//
// Self-contained tests for the De-hum core: no test framework needed.

#include "mend/DeHum.hpp"

#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

using namespace mend;

static int failures = 0;

#define CHECK(cond, ...)                                   \
    do {                                                   \
        if (!(cond)) {                                     \
            ++failures;                                    \
            std::printf("  FAIL %s:%d: ", __FILE__, __LINE__); \
            std::printf(__VA_ARGS__);                      \
            std::printf("\n");                             \
        }                                                  \
    } while (0)

// Amplitude of a sinusoid at `f` in x[from..to), via correlation.
static double toneAmplitude(const std::vector<float>& x, double fs, double f, size_t from, size_t to)
{
    double re = 0.0, im = 0.0;
    for (size_t i = from; i < to; ++i) {
        const double w = 2.0 * kPi * f * static_cast<double>(i) / fs;
        re += x[i] * std::cos(w);
        im += x[i] * std::sin(w);
    }
    return 2.0 * std::sqrt(re * re + im * im) / static_cast<double>(to - from);
}

struct TestSignal {
    std::vector<float> wanted; // programme material
    std::vector<float> mixed;  // programme + hum
};

static TestSignal makeSignal(double fs, double seconds, double humHz, double humAmp, unsigned seed)
{
    const size_t n = static_cast<size_t>(fs * seconds);
    TestSignal t;
    t.wanted.resize(n);
    t.mixed.resize(n);
    std::mt19937 rng(seed);
    std::normal_distribution<double> noise(0.0, 0.01);
    for (size_t i = 0; i < n; ++i) {
        const double tt = static_cast<double>(i) / fs;
        // "music": two steady tones well away from hum harmonics, plus noise
        const double prog = 0.2 * std::sin(2.0 * kPi * 440.0 * tt) + 0.1 * std::sin(2.0 * kPi * 1234.5 * tt) + noise(rng);
        // hum: fundamental plus decaying harmonics (typical mains buzz)
        double hum = 0.0;
        for (int h = 1; h <= 6; ++h)
            hum += (humAmp / h) * std::sin(2.0 * kPi * humHz * h * tt + 0.3 * h);
        t.wanted[i] = static_cast<float>(prog);
        t.mixed[i] = static_cast<float>(prog + hum);
    }
    return t;
}

static std::vector<float> runDeHum(DeHum& dh, const std::vector<float>& input, double fs, int block)
{
    dh.prepare(fs, block, 1);
    std::vector<float> out(input.size());
    for (size_t pos = 0; pos < input.size(); pos += block) {
        const int n = static_cast<int>(std::min<size_t>(block, input.size() - pos));
        const float* in[1] = { input.data() + pos };
        float* o[1] = { out.data() + pos };
        dh.process(in, o, 1, n);
    }
    return out;
}

static void testDetection(double fs, double humHz)
{
    std::printf("detection: %.2f Hz hum at %.0f Hz\n", humHz, fs);
    auto sig = makeSignal(fs, 4.0, humHz, 0.05, 1);
    DeHum dh;
    DeHum::Settings s;
    s.autoDetect = true;
    dh.setSettings(s);
    runDeHum(dh, sig.mixed, fs, 512);
    const auto& r = dh.detection();
    std::printf("  detected %.3f Hz, confidence %.2f, locked %d\n", r.frequency, r.confidence, r.locked);
    CHECK(r.locked, "detector should lock onto an obvious hum");
    CHECK(std::abs(r.frequency - humHz) < 0.05, "detected %.3f, expected %.3f", r.frequency, humHz);
}

static void testRemoval(double fs, double humHz, bool autoDetect)
{
    std::printf("removal: %.2f Hz hum at %.0f Hz (%s)\n", humHz, fs, autoDetect ? "auto" : "manual");
    auto sig = makeSignal(fs, 6.0, humHz, 0.05, 2);
    DeHum dh;
    DeHum::Settings s;
    s.autoDetect = autoDetect;
    s.frequency = humHz;
    s.harmonics = 8;
    s.q = 40.0;
    s.reductionDb = 40.0;
    dh.setSettings(s);
    auto out = runDeHum(dh, sig.mixed, fs, 256);

    // Measure over the last 2 seconds, after detection and filters settle.
    const size_t from = static_cast<size_t>(fs * 4.0), to = sig.mixed.size();
    for (int h = 1; h <= 6; ++h) {
        const double before = toneAmplitude(sig.mixed, fs, humHz * h, from, to);
        const double after = toneAmplitude(out, fs, humHz * h, from, to);
        const double reduction = gainToDb(before) - gainToDb(after);
        std::printf("  harmonic %d (%.1f Hz): reduced by %.1f dB\n", h, humHz * h, reduction);
        CHECK(reduction > 25.0, "harmonic %d only reduced by %.1f dB", h, reduction);
    }
    for (double f : { 440.0, 1234.5 }) {
        const double before = toneAmplitude(sig.wanted, fs, f, from, to);
        const double after = toneAmplitude(out, fs, f, from, to);
        const double change = gainToDb(after) - gainToDb(before);
        std::printf("  programme tone %.1f Hz changed by %+.2f dB\n", f, change);
        CHECK(std::abs(change) < 0.5, "programme tone at %.1f changed by %.2f dB", f, change);
    }
}

static void testHumOnlyIsComplement()
{
    std::printf("hum-only output equals input minus processed output\n");
    const double fs = 48000.0;
    auto sig = makeSignal(fs, 2.0, 50.0, 0.05, 3);
    DeHum a, b;
    DeHum::Settings s;
    s.autoDetect = false;
    a.setSettings(s);
    s.humOnly = true;
    b.setSettings(s);
    auto clean = runDeHum(a, sig.mixed, fs, 128);
    auto hum = runDeHum(b, sig.mixed, fs, 128);
    double maxErr = 0.0;
    for (size_t i = 0; i < clean.size(); ++i)
        maxErr = std::max(maxErr, static_cast<double>(std::abs(sig.mixed[i] - clean[i] - hum[i])));
    std::printf("  max error %.2e\n", maxErr);
    CHECK(maxErr < 1e-5, "complement error %.2e", maxErr);
}

static void testSilenceStaysSilent()
{
    std::printf("silence in, silence out\n");
    std::vector<float> zeros(48000 * 3, 0.0f);
    DeHum dh;
    dh.setSettings(DeHum::Settings{});
    auto out = runDeHum(dh, zeros, 48000.0, 512);
    double peak = 0.0;
    for (float v : out)
        peak = std::max(peak, static_cast<double>(std::abs(v)));
    CHECK(peak == 0.0, "silence produced output peak %.3e", peak);
    CHECK(!dh.detection().locked, "detector should not lock on silence");
}

// Music whose partials are exact harmonics of 55 Hz (A1): 220, 330, 440,
// 660 Hz. A naive harmonic-sum detector "finds hum" at 55 Hz here and
// would notch the notes out.
static void testMusicIsNotHum()
{
    std::printf("music in A (partials on 55 Hz harmonics) is not mistaken for hum\n");
    const double fs = 48000.0;
    const size_t n = static_cast<size_t>(fs * 8.0);
    std::vector<float> x(n);
    for (size_t i = 0; i < n; ++i) {
        const double t = i / fs;
        x[i] = static_cast<float>(0.12 * (std::sin(2 * kPi * 220 * t) + 0.6 * std::sin(2 * kPi * 330 * t)
                                       + 0.5 * std::sin(2 * kPi * 440 * t) + 0.3 * std::sin(2 * kPi * 660 * t)));
    }
    DeHum dh;
    dh.setSettings(DeHum::Settings{});
    auto out = runDeHum(dh, x, fs, 512);
    std::printf("  locked %d, frequency in use %.2f Hz\n", dh.detection().everLocked, dh.currentFrequency());
    CHECK(!dh.detection().everLocked, "locked onto music as hum");
    // with no hum found, auto mode passes audio through untouched
    for (double f : { 220.0, 330.0, 440.0, 660.0 }) {
        const double before = toneAmplitude(x, fs, f, n / 2, n), after = toneAmplitude(out, fs, f, n / 2, n);
        const double change = gainToDb(after) - gainToDb(before);
        std::printf("  %.0f Hz note changed by %+.2f dB\n", f, change);
        CHECK(std::abs(change) < 0.01, "note at %.0f Hz changed by %.2f dB", f, change);
    }
}

// A bass note sustained for a few seconds at 49 Hz (G1) changes to other
// notes before the detector's agreement window closes.
static void testChangingBassIsNotHum()
{
    std::printf("a moving bass line inside the search band is not locked\n");
    const double fs = 48000.0;
    const double notes[] = { 49.0, 61.7, 51.9, 58.3, 49.0, 61.7, 51.9, 58.3, 49.0, 61.7, 51.9, 58.3 };
    const size_t per = static_cast<size_t>(fs * 0.6);
    std::vector<float> x(per * 12);
    double ph = 0.0;
    for (size_t i = 0; i < x.size(); ++i) {
        ph += 2.0 * kPi * notes[i / per] / fs;
        x[i] = static_cast<float>(0.3 * std::sin(ph));
    }
    DeHum dh;
    dh.setSettings(DeHum::Settings{});
    runDeHum(dh, x, fs, 512);
    std::printf("  locked %d\n", dh.detection().everLocked);
    CHECK(!dh.detection().everLocked, "locked onto a bass line");
}

// Pure generated tones (no noise at all) must not be mistaken for hum.
static void testPureTonesAreNotHum()
{
    std::printf("pure test tones at 44.1/48 kHz are not mistaken for hum\n");
    for (double fs : { 44100.0, 48000.0 })
        for (double f : { 220.0, 440.0, 1000.0, 3000.0 }) {
            const size_t n = static_cast<size_t>(fs * 8.0);
            std::vector<float> x(n);
            for (size_t i = 0; i < n; ++i)
                x[i] = static_cast<float>(0.8 * std::sin(2.0 * kPi * f * i / fs));
            DeHum dh;
            dh.setSettings(DeHum::Settings{});
            runDeHum(dh, x, fs, 8192);
            CHECK(!dh.detection().everLocked, "locked onto a pure %.0f Hz tone at %.0f Hz", f, fs);
        }
}

int main()
{
    testPureTonesAreNotHum();
    testMusicIsNotHum();
    testChangingBassIsNotHum();
    testDetection(48000.0, 50.0);
    testDetection(44100.0, 60.0);
    testDetection(96000.0, 49.83);
    testRemoval(48000.0, 50.0, false);
    testRemoval(44100.0, 59.9, true);
    testRemoval(96000.0, 50.2, true);
    testHumOnlyIsComplement();
    testSilenceStaysSilent();

    if (failures) {
        std::printf("\n%d check(s) FAILED\n", failures);
        return 1;
    }
    std::printf("\nall checks passed\n");
    return 0;
}
