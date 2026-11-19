// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#include "TestUtil.hpp"
#include "mend/Biquad.hpp"
#include "mend/VoiceClean.hpp"

#include <cstdint>

using namespace mend;
using namespace mendtest;

namespace {
// Deterministic noise (the same on every standard library).
struct Lcg {
    uint32_t s;
    double next() // -1..1
    {
        s = s * 1664525u + 1013904223u;
        return static_cast<double>(s >> 8) / 8388608.0 - 1.0;
    }
};

// Speech-like test signal: a pitched glottal buzz through moving vowel
// formants, broken into syllables, with noise bursts for consonants.
std::vector<float> speechLike(double fs, double seconds, double levelDb, uint32_t seed)
{
    const size_t n = static_cast<size_t>(fs * seconds);
    std::vector<float> x(n);
    Lcg rng { seed };
    const double formants[3][3] = { { 700, 1200, 2600 }, { 300, 2300, 3000 }, { 500, 900, 2400 } };
    Biquad f[3], hiss;
    hiss.setCoeffs(BiquadCoeffs::highpass(fs, 4000.0, 0.7));
    double phase = 0.0;
    int vowel = 0;
    for (size_t i = 0; i < n; ++i) {
        const double t = i / fs;
        const double syl = std::fmod(t, 0.25) / 0.25; // 4 syllables a second
        if (i % static_cast<size_t>(fs * 0.25) == 0) {
            vowel = (vowel + 1) % 3;
            for (int k = 0; k < 3; ++k)
                f[k].setCoeffs(BiquadCoeffs::peaking(fs, formants[vowel][k], 4.0, dbToGain(18.0 - 4.0 * k)));
        }
        const double f0 = 120.0 * (1.0 + 0.15 * std::sin(2.0 * kPi * 0.7 * t));
        phase += f0 / fs;
        if (phase >= 1.0)
            phase -= 1.0;
        double v = (phase < 0.4 ? std::sin(kPi * phase / 0.4) : 0.0) - 0.25; // glottal pulse
        for (auto& b : f)
            v = b.process(v);
        const double env = syl < 0.75 ? std::sin(kPi * syl / 0.75) : 0.0;
        double s = v * env * 0.08;
        if (syl > 0.8) // consonant
            s += hiss.process(rng.next()) * 0.15 * std::sin(kPi * (syl - 0.8) / 0.2);
        x[i] = static_cast<float>(s);
    }
    // scale to the requested RMS
    const double r = rms(x, 0, n);
    const double g = dbToGain(levelDb) / (r + 1e-12);
    for (auto& v : x)
        v = static_cast<float>(v * g);
    return x;
}

std::vector<float> run(VoiceClean& v, const std::vector<float>& x)
{
    std::vector<float> l = x, r = x, ol(x.size()), orr(x.size());
    for (size_t at = 0; at < x.size(); at += 480) {
        const int n = static_cast<int>(std::min<size_t>(480, x.size() - at));
        const float* in[2] = { l.data() + at, r.data() + at };
        float* out[2] = { ol.data() + at, orr.data() + at };
        v.process(in, out, 2, n);
    }
    return ol;
}

double bandRms(const std::vector<float>& x, double fs, double hz, size_t from, size_t to, double q = 4.0)
{
    Biquad b1, b2;
    const double w0 = 2.0 * kPi * hz / fs, alpha = std::sin(w0) / (2.0 * q), a0 = 1.0 + alpha;
    BiquadCoeffs bp { alpha / a0, 0.0, -alpha / a0, -2.0 * std::cos(w0) / a0, (1.0 - alpha) / a0 };
    b1.setCoeffs(bp);
    b2.setCoeffs(bp);
    double s = 0.0;
    for (size_t i = 0; i < to; ++i) {
        const double y = b2.process(b1.process(x[i]));
        if (i >= from)
            s += y * y;
    }
    return std::sqrt(s / (to - from));
}

VoiceClean::Settings knobs(double clarity, double smooth, double deMud, int character = 0)
{
    VoiceClean::Settings s;
    s.clarity = clarity;
    s.smooth = smooth;
    s.deMud = deMud;
    s.character = character;
    return s;
}
} // namespace

static void testProfiles()
{
    std::printf("Microphone profiles and characters are well formed\n");
    CHECK(VoiceClean::numMics() >= 20, "only %d microphone profiles", VoiceClean::numMics());
    for (int i = 0; i < VoiceClean::numMics(); ++i) {
        const auto& m = VoiceClean::mic(i);
        CHECK(m.highPassHz >= 40 && m.highPassHz <= 200, "%s: odd high-pass %.0f", m.name, m.highPassHz);
        for (const auto& b : m.bands)
            CHECK(std::abs(b.db) <= 6.0, "%s: correction of %.1f dB is too strong", m.name, b.db);
    }
    CHECK(VoiceClean::numCharacters() == 5, "%d characters", VoiceClean::numCharacters());
}

static void testSafeAndBounded()
{
    std::printf("Every profile and character: finite output, peaks under -1 dBFS\n");
    const double fs = 48000.0;
    auto x = speechLike(fs, 2.0, -12.0, 1);
    for (int m = 0; m < VoiceClean::numMics(); ++m) {
        VoiceClean v;
        v.prepare(fs, 2);
        auto s = knobs(100, 100, 100, m % VoiceClean::numCharacters());
        s.mic = m;
        s.outputDb = 12.0;
        v.setSettings(s);
        auto y = run(v, x);
        float peak = 0.0f;
        bool finite = true;
        for (float f : y) {
            finite = finite && std::isfinite(f);
            peak = std::max(peak, std::abs(f));
        }
        CHECK(finite, "%s: non-finite output", VoiceClean::mic(m).name);
        CHECK(peak <= dbToGain(-1.0) + 1e-6, "%s: peak %.2f dBFS", VoiceClean::mic(m).name, gainToDb(peak));
    }
}

static void testNeutralAtZero()
{
    std::printf("All controls at zero, flat, generic mic: level unchanged\n");
    const double fs = 48000.0;
    auto x = speechLike(fs, 4.0, -24.0, 2);
    VoiceClean v;
    v.prepare(fs, 2);
    v.setSettings(knobs(0, 0, 0));
    auto y = run(v, x);
    const size_t L = v.latency();
    // Compare above the 80 Hz high-pass, which is always on.
    auto above = [fs](std::vector<float> s) {
        Biquad a, b;
        a.setCoeffs(BiquadCoeffs::highpass(fs, 200.0, 0.5412));
        b.setCoeffs(BiquadCoeffs::highpass(fs, 200.0, 1.3066));
        for (auto& v : s)
            v = static_cast<float>(b.process(a.process(v)));
        return s;
    };
    x = above(x);
    y = above(y);
    const double change = gainToDb(rms(y, fs + L, x.size()) / rms(x, fs, x.size() - L));
    std::printf("  level change %.2f dB (latency %zu samples)\n", change, L);
    CHECK(std::abs(change) < 0.5, "level changed by %.2f dB", change);
}

static void testLatencyAligned()
{
    std::printf("Reported latency lines the output up with the input\n");
    const double fs = 48000.0;
    auto x = speechLike(fs, 3.0, -24.0, 3);
    VoiceClean v;
    v.prepare(fs, 2);
    v.setSettings(knobs(0, 0, 0));
    auto y = run(v, x);
    const int L = v.latency();
    int best = 0;
    double bestC = -1e30;
    for (int lag = L - 200; lag <= L + 200; ++lag) {
        double c = 0.0;
        for (size_t i = static_cast<size_t>(fs); i < static_cast<size_t>(2 * fs); ++i)
            c += static_cast<double>(x[i]) * y[i + lag];
        if (c > bestC) {
            bestC = c;
            best = lag;
        }
    }
    std::printf("  best alignment %d samples, reported %d\n", best, L);
    // The crossover and high-pass shift the waveform by a few samples.
    CHECK(std::abs(best - L) <= 24, "aligned at %d, reported %d", best, L);
}

static void testLevelling()
{
    std::printf("Smooth evens out a quiet and a loud speaker\n");
    const double fs = 48000.0;
    auto quiet = speechLike(fs, 6.0, -40.0, 4), loud = speechLike(fs, 6.0, -14.0, 5);
    double spread[2];
    for (int k = 0; k < 2; ++k) {
        const double smooth = k ? 100.0 : 0.0;
        VoiceClean a, b;
        a.prepare(fs, 2);
        b.prepare(fs, 2);
        a.setSettings(knobs(0, smooth, 0));
        b.setSettings(knobs(0, smooth, 0));
        auto qa = run(a, quiet), lb = run(b, loud);
        spread[k] = gainToDb(rms(lb, 3 * fs, 6 * fs) / rms(qa, 3 * fs, 6 * fs));
    }
    std::printf("  loud - quiet: %.1f dB at Smooth 0, %.1f dB at Smooth 100\n", spread[0], spread[1]);
    CHECK(spread[1] < spread[0] - 12.0, "spread only fell from %.1f to %.1f dB", spread[0], spread[1]);
}

static void testDeMud()
{
    std::printf("De-mud cuts low-mid build-up, and only when it is there\n");
    const double fs = 48000.0;
    // The synthetic voice is heavy in the low mids; balance it first.
    auto clean = speechLike(fs, 5.0, -22.0, 6);
    Biquad tilt;
    tilt.setCoeffs(BiquadCoeffs::peaking(fs, 400.0, 0.6, dbToGain(-8.0)));
    for (auto& s : clean)
        s = static_cast<float>(tilt.process(s));
    auto muddy = clean;
    Biquad boost;
    boost.setCoeffs(BiquadCoeffs::peaking(fs, 300.0, 1.0, dbToGain(9.0)));
    for (auto& s : muddy)
        s = static_cast<float>(boost.process(s));
    auto cut = [&](const std::vector<float>& x) {
        VoiceClean off, on;
        off.prepare(fs, 2);
        on.prepare(fs, 2);
        off.setSettings(knobs(0, 0, 0));
        on.setSettings(knobs(0, 0, 100));
        auto a = run(off, x), b = run(on, x);
        return gainToDb(bandRms(a, fs, 300, 2 * fs, 5 * fs) / bandRms(b, fs, 300, 2 * fs, 5 * fs));
    };
    const double onMuddy = cut(muddy), onClean = cut(clean);
    std::printf("  300 Hz cut: %.1f dB on a muddy voice, %.1f dB on a clean one\n", onMuddy, onClean);
    CHECK(onMuddy > 5.0, "only %.1f dB cut on the muddy voice", onMuddy);
    CHECK(onClean < onMuddy - 3.0, "clean voice cut nearly as much (%.1f dB)", onClean);
}

static void testClarity()
{
    std::printf("Clarity lifts consonants more than vowels\n");
    const double fs = 48000.0;
    auto x = speechLike(fs, 4.0, -22.0, 7);
    VoiceClean off, on;
    off.prepare(fs, 2);
    on.prepare(fs, 2);
    off.setSettings(knobs(0, 0, 0));
    on.setSettings(knobs(100, 0, 0));
    auto a = run(off, x), b = run(on, x);
    const double hi = gainToDb(bandRms(b, fs, 6000, fs, 4 * fs) / bandRms(a, fs, 6000, fs, 4 * fs));
    const double lo = gainToDb(bandRms(b, fs, 400, fs, 4 * fs) / bandRms(a, fs, 400, fs, 4 * fs));
    std::printf("  6 kHz %+.1f dB, 400 Hz %+.1f dB\n", hi, lo);
    CHECK(hi > 2.0 && hi < 10.0, "6 kHz changed by %+.1f dB", hi);
    CHECK(std::abs(lo) < 1.0, "400 Hz changed by %+.1f dB", lo);
}

static void testResonance()
{
    std::printf("A fixed resonance is smoothed, the voice around it is not\n");
    const double fs = 48000.0;
    auto x = speechLike(fs, 8.0, -24.0, 8);
    Biquad ring;
    ring.setCoeffs(BiquadCoeffs::peaking(fs, 2500.0, 12.0, dbToGain(12.0)));
    for (auto& s : x)
        s = static_cast<float>(ring.process(s));
    VoiceClean off, on;
    off.prepare(fs, 2);
    on.prepare(fs, 2);
    off.setSettings(knobs(0, 0, 0));
    on.setSettings(knobs(0, 100, 0)); // Smooth deepens the resonance smoother
    auto a = run(off, x), b = run(on, x);
    // compare the ringing band with its neighbourhood (levelling cancels out)
    const double at = gainToDb(bandRms(b, fs, 2500, 5 * fs, 8 * fs, 12.0) / bandRms(a, fs, 2500, 5 * fs, 8 * fs, 12.0));
    const double near = gainToDb(bandRms(b, fs, 1600, 5 * fs, 8 * fs, 12.0) / bandRms(a, fs, 1600, 5 * fs, 8 * fs, 12.0));
    std::printf("  2.5 kHz %+.1f dB, 1.6 kHz %+.1f dB; deepest cut %.1f dB\n", at, near,
                on.readout().resonanceDb.load());
    CHECK(at < near - 2.0, "resonance only %.1f dB below its surroundings", near - at);
}

int main()
{
    testProfiles();
    testSafeAndBounded();
    testNeutralAtZero();
    testLatencyAligned();
    testLevelling();
    testDeMud();
    testClarity();
    testResonance();
    return finish();
}
