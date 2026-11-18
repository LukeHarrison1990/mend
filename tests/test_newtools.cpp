// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
// Tests for EQ, De-ess, Leveler, Breath Control, De-reverb, De-wind and
// Center Extract, on synthetic signals with known answers.
#include "TestUtil.hpp"

#include "mend/Dynamics.hpp"
#include "mend/Eq.hpp"
#include "mend/SpectralTools.hpp"

using namespace mend;
using namespace mendtest;

namespace {
constexpr double kFs = 48000.0;

// Amplitude of a sine at `hz` in x[from..to) (single-bin DFT).
double toneAmp(const std::vector<float>& x, double hz, size_t from, size_t to)
{
    double re = 0, im = 0;
    for (size_t i = from; i < to; ++i) {
        const double w = 2 * kPi * hz * i / kFs;
        re += x[i] * std::cos(w);
        im -= x[i] * std::sin(w);
    }
    return 2.0 * std::hypot(re, im) / static_cast<double>(to - from);
}

std::vector<float> filtered(const std::vector<float>& x, BiquadCoeffs c, int times = 2)
{
    std::vector<float> y = x;
    for (int t = 0; t < times; ++t) {
        Biquad b;
        b.setCoeffs(c);
        for (auto& v : y)
            v = static_cast<float>(b.process(v));
    }
    return y;
}

std::vector<float> noise(size_t n, double sigma, unsigned seed)
{
    std::mt19937 rng(seed);
    std::normal_distribution<double> d(0.0, sigma);
    std::vector<float> x(n);
    for (auto& v : x)
        v = static_cast<float>(d(rng));
    return x;
}

// Run a processor and remove its latency.
template <typename P>
std::vector<float> runAligned(P& p, const std::vector<float>& x)
{
    std::vector<float> in = x;
    in.resize(x.size() + static_cast<size_t>(p.latency()), 0.0f);
    auto y = runMono(p, in, 512);
    return std::vector<float>(y.begin() + p.latency(), y.end());
}

double db(double v) { return 20.0 * std::log10(v + 1e-20); }
} // namespace

static void testEq()
{
    std::printf("EQ: flat is transparent, bands hit their marks\n");
    ParametricEq eq;
    eq.prepare(kFs, 1);
    const auto x = musicLike(kFs, 1.0, 1);
    auto y = runMono(eq, x, 256);
    CHECK(rmsDiff(x, y, 0, x.size()) == 0.0, "flat EQ changed the audio");

    ParametricEq::Settings s;
    s.peakDb = { 0.0, 6.0, 0.0 };
    s.peakHz = { 300.0, 1000.0, 4000.0 };
    s.highPassHz = 100.0;
    eq.setSettings(s);
    CHECK(std::abs(eq.responseDb(1000.0) - 6.0) < 0.1, "peak band reads %.2f dB", eq.responseDb(1000.0));
    CHECK(eq.responseDb(30.0) < -35.0, "high-pass only %.1f dB at 30 Hz", eq.responseDb(30.0));
    std::vector<float> tone(48000);
    for (size_t i = 0; i < tone.size(); ++i)
        tone[i] = static_cast<float>(0.1 * std::sin(2 * kPi * 1000.0 * i / kFs));
    eq.reset();
    y = runMono(eq, tone, 256);
    const double g = db(toneAmp(y, 1000.0, 24000, 48000) / 0.1);
    CHECK(std::abs(g - 6.0) < 0.2, "1 kHz tone gained %.2f dB", g);
    s.highShelfDb = -12.0;
    s.highShelfHz = 6000.0;
    eq.setSettings(s);
    CHECK(eq.responseDb(18000.0) < -10.0, "high shelf only %.1f dB at 18 kHz", eq.responseDb(18000.0));
}

static void testDeEss()
{
    std::printf("De-ess: sibilant bursts down, the voice below untouched\n");
    const size_t n = static_cast<size_t>(kFs * 3);
    std::vector<float> voice(n), sib = filtered(noise(n, 0.15, 7), BiquadCoeffs::highpass(kFs, 6000, 0.707), 3);
    for (size_t i = 0; i < n; ++i)
        voice[i] = static_cast<float>(0.2 * std::sin(2 * kPi * 300.0 * i / kFs));
    // sibilance only in 0.5 s bursts every second
    for (size_t i = 0; i < n; ++i)
        if ((i / static_cast<size_t>(kFs / 2)) % 2 == 0)
            sib[i] = 0.0f;
    std::vector<float> x(n);
    for (size_t i = 0; i < n; ++i)
        x[i] = voice[i] + sib[i];
    DeEss d;
    d.prepare(kFs, 1);
    DeEss::Settings s;
    s.thresholdDb = -30.0;
    s.maxReductionDb = 15.0;
    d.setSettings(s);
    const auto y = runAligned(d, x);
    const auto hiIn = filtered(x, BiquadCoeffs::highpass(kFs, 6000, 0.707), 3);
    const auto hiOut = filtered(y, BiquadCoeffs::highpass(kFs, 6000, 0.707), 3);
    const size_t a = static_cast<size_t>(kFs * 0.6), b = static_cast<size_t>(kFs * 0.95); // inside a burst
    const double red = db(rms(hiIn, a, b) / rms(hiOut, a, b));
    CHECK(red > 8.0, "sibilance reduced only %.1f dB", red);
    const double voiceIn = toneAmp(x, 300.0, 0, static_cast<size_t>(kFs * 0.45));
    const double voiceOut = toneAmp(y, 300.0, 0, static_cast<size_t>(kFs * 0.45));
    CHECK(std::abs(db(voiceOut / voiceIn)) < 0.5, "voice changed %.2f dB between bursts", db(voiceOut / voiceIn));
}

static void testLeveler()
{
    std::printf("Leveler: quiet and loud passages brought together\n");
    const auto quiet = musicLike(kFs, 6.0, 3, 0.02);
    const auto loud = musicLike(kFs, 6.0, 4, 0.3);
    std::vector<float> x = quiet;
    x.insert(x.end(), loud.begin(), loud.end());
    Leveler l;
    l.prepare(kFs, 1);
    Leveler::Settings s;
    s.targetDb = -20.0;
    s.speedSeconds = 0.5;
    s.maxBoostDb = 24.0;
    l.setSettings(s);
    const auto y = runAligned(l, x);
    const size_t q0 = static_cast<size_t>(kFs * 4), q1 = static_cast<size_t>(kFs * 6);
    const size_t l0 = static_cast<size_t>(kFs * 10), l1 = static_cast<size_t>(kFs * 12);
    const double before = db(rms(x, l0, l1) / rms(x, q0, q1));
    const double after = db(rms(y, l0, l1) / rms(y, q0, q1));
    CHECK(before > 20.0 && after < 6.0, "level gap %.1f dB -> %.1f dB", before, after);
}

static void testBreathControl()
{
    std::printf("Breath Control: breaths turned down, speech untouched\n");
    // phrases of a voiced buzz (150 Hz, formant-ish) separated by breaths
    const size_t n = static_cast<size_t>(kFs * 6);
    std::vector<float> x(n, 0.0f);
    auto breath = filtered(noise(n, 0.02, 9), BiquadCoeffs::lowpass(kFs, 4000, 0.707), 2);
    breath = filtered(breath, BiquadCoeffs::highpass(kFs, 500, 0.707), 2);
    std::vector<char> isBreath(n, 0);
    for (size_t i = 0; i < n; ++i) {
        const double t = i / kFs, ph = std::fmod(t, 1.5);
        if (ph < 1.0) {
            double v = 0;
            for (int h = 1; h < 20; ++h)
                v += (h < 6 ? 1.0 : 0.3) / h * std::sin(2 * kPi * 150.0 * h * t);
            x[i] = static_cast<float>(0.15 * v * std::sin(kPi * ph));
        } else if (ph > 1.1 && ph < 1.4) {
            x[i] = breath[i];
            isBreath[i] = 1;
        }
    }
    BreathControl b;
    b.prepare(kFs, 1);
    BreathControl::Settings s;
    s.reductionDb = 20.0;
    b.setSettings(s);
    const auto y = runAligned(b, x);
    double eIn = 0, eOut = 0, vIn = 0, vOut = 0;
    for (size_t i = static_cast<size_t>(kFs * 1.5); i < n; ++i) { // after the first phrase sets the speech level
        const double t = std::fmod(i / kFs, 1.5);
        if (isBreath[i] && t > 1.17 && t < 1.33) {
            eIn += x[i] * x[i];
            eOut += y[i] * y[i];
        } else if (t > 0.2 && t < 0.8) {
            vIn += x[i] * x[i];
            vOut += y[i] * y[i];
        }
    }
    const double red = 10 * std::log10(eIn / (eOut + 1e-20));
    const double vch = 10 * std::log10(vOut / vIn);
    CHECK(red > 10.0, "breaths reduced only %.1f dB", red);
    CHECK(std::abs(vch) < 0.5, "speech changed %.2f dB", vch);
}

static void testDeReverb()
{
    std::printf("De-reverb: reverb tail reduced, direct sound kept\n");
    // dry: short bursts every 1.5 s; wet: bursts convolved with a 1 s T60 noise tail
    const size_t n = static_cast<size_t>(kFs * 6);
    std::vector<float> dry(n, 0.0f);
    auto burst = noise(static_cast<size_t>(kFs * 0.03), 0.3, 11);
    for (size_t start = 0; start + burst.size() < n; start += static_cast<size_t>(kFs * 1.5))
        for (size_t i = 0; i < burst.size(); ++i)
            dry[start + i] = burst[i];
    auto ir = noise(static_cast<size_t>(kFs * 1.2), 1.0, 12);
    const double delta = 3 * std::log(10.0) / 1.0;
    for (size_t i = 0; i < ir.size(); ++i)
        ir[i] = static_cast<float>(ir[i] * 0.02 * std::exp(-delta * i / kFs));
    ir[0] = 1.0f;
    std::vector<float> wet(n, 0.0f);
    for (size_t i = 0; i < n; ++i)
        if (dry[i] != 0.0f)
            for (size_t k = 0; k < ir.size() && i + k < n; ++k)
                wet[i + k] += dry[i] * ir[k];
    DeReverb d;
    d.prepare(kFs, 1);
    DeReverb::Settings s;
    s.reverbTime = 1.0;
    s.amount = 80.0;
    d.setSettings(s);
    const auto y = runAligned(d, wet);
    const size_t b = static_cast<size_t>(kFs * 1.5); // second burst
    const double tail = db(rms(y, b + static_cast<size_t>(kFs * 0.15), b + static_cast<size_t>(kFs * 0.6))
                           / rms(wet, b + static_cast<size_t>(kFs * 0.15), b + static_cast<size_t>(kFs * 0.6)));
    // the direct sound should survive: compare with the dry burst itself
    const double direct = db(rms(y, b, b + static_cast<size_t>(kFs * 0.03)) / rms(dry, b, b + static_cast<size_t>(kFs * 0.03)));
    CHECK(tail < -6.0, "tail changed only %.1f dB", tail);
    CHECK(direct > -2.0, "direct sound changed %.1f dB", direct);
    std::printf("  tail %.1f dB, direct vs dry %.1f dB\n", tail, direct);
}

static void testDeWind()
{
    std::printf("De-wind: rumble below the cutoff reduced, music above kept\n");
    const size_t n = static_cast<size_t>(kFs * 4);
    auto music = musicLike(kFs, 4.0, 5, 0.2);
    auto wind = filtered(noise(n, 8.0, 13), BiquadCoeffs::lowpass(kFs, 80, 0.707), 2); // gusty rumble, about as loud as the music
    std::vector<float> x(n);
    for (size_t i = 0; i < n; ++i)
        x[i] = music[i] + wind[i];
    DeWind d;
    d.prepare(kFs, 1);
    DeWind::Settings s;
    s.frequency = 180.0;
    s.strength = 90.0;
    d.setSettings(s);
    const auto y = runAligned(d, x);
    const auto lowIn = filtered(x, BiquadCoeffs::lowpass(kFs, 90, 0.707), 3);
    const auto lowOut = filtered(y, BiquadCoeffs::lowpass(kFs, 90, 0.707), 3);
    const size_t a = static_cast<size_t>(kFs), b = n - static_cast<size_t>(kFs * 0.2);
    const double red = db(rms(lowIn, a, b) / rms(lowOut, a, b));
    CHECK(red > 8.0, "wind reduced only %.1f dB", red);
    const double keep = db(toneAmp(y, 440.0, a, b) / toneAmp(x, 440.0, a, b));
    CHECK(std::abs(keep) < 0.5, "440 Hz partial changed %.2f dB", keep);
}

static void testCenterExtract()
{
    std::printf("Center Extract: keeps or removes what both channels share\n");
    const size_t n = static_cast<size_t>(kFs * 3);
    std::vector<float> L(n), R(n);
    for (size_t i = 0; i < n; ++i) {
        const double c = 0.2 * std::sin(2 * kPi * 500.0 * i / kFs);
        L[i] = static_cast<float>(c + 0.2 * std::sin(2 * kPi * 1300.0 * i / kFs));
        R[i] = static_cast<float>(c + 0.2 * std::sin(2 * kPi * 2100.0 * i / kFs));
    }
    for (bool remove : { false, true }) {
        CenterExtract ce;
        ce.prepare(kFs, 2);
        CenterExtract::Settings s;
        s.removeCenter = remove;
        ce.setSettings(s);
        const int lat = ce.latency();
        std::vector<float> li = L, ri = R;
        li.resize(n + lat, 0.0f);
        ri.resize(n + lat, 0.0f);
        std::vector<float> lo(n + lat), ro(n + lat);
        for (size_t pos = 0; pos < n + lat; pos += 512) {
            const int m = static_cast<int>(std::min<size_t>(512, n + lat - pos));
            const float* in[2] = { li.data() + pos, ri.data() + pos };
            float* out[2] = { lo.data() + pos, ro.data() + pos };
            ce.process(in, out, 2, m);
        }
        std::vector<float> l(lo.begin() + lat, lo.end());
        const size_t a = static_cast<size_t>(kFs), b = n - static_cast<size_t>(kFs * 0.2);
        const double centre = db(toneAmp(l, 500.0, a, b) / 0.2);
        const double side = db(toneAmp(l, 1300.0, a, b) / 0.2);
        if (remove) {
            CHECK(centre < -15.0, "remove: centre only %.1f dB down", centre);
            CHECK(side > -1.5, "remove: side changed %.1f dB", side);
        } else {
            CHECK(centre > -1.5, "keep: centre changed %.1f dB", centre);
            CHECK(side < -15.0, "keep: side only %.1f dB down", side);
        }
    }
}

int main()
{
    testEq();
    testDeEss();
    testLeveler();
    testBreathControl();
    testDeReverb();
    testDeWind();
    testCenterExtract();
    return finish();
}
