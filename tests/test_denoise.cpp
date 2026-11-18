// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#include "TestUtil.hpp"
#include "mend/SpectralDenoise.hpp"

using namespace mend;
using namespace mendtest;

// "Speech": bursts of tones with gaps, so there are noise-only passages.
static std::vector<float> phrases(double fs, double seconds)
{
    const size_t n = static_cast<size_t>(fs * seconds);
    std::vector<float> x(n, 0.0f);
    for (size_t i = 0; i < n; ++i) {
        const double t = i / fs;
        const double cycle = std::fmod(t, 1.0);
        if (cycle < 0.55) { // 0.55 s "phrase", 0.45 s pause
            const double env = std::sin(kPi * cycle / 0.55);
            x[i] = static_cast<float>(env * (0.2 * std::sin(2 * kPi * 330 * t) + 0.1 * std::sin(2 * kPi * 990 * t)
                                             + 0.05 * std::sin(2 * kPi * 2310 * t)));
        }
    }
    return x;
}

static std::vector<float> noise(size_t n, double sigma, unsigned seed)
{
    std::mt19937 rng(seed);
    std::normal_distribution<double> d(0.0, sigma);
    std::vector<float> v(n);
    for (auto& s : v)
        s = static_cast<float>(d(rng));
    return v;
}

// Correlation-based amplitude of a tone in [from, to).
static double tone(const std::vector<float>& x, double fs, double f, size_t from, size_t to)
{
    double re = 0, im = 0;
    for (size_t i = from; i < to; ++i) {
        re += x[i] * std::cos(2 * kPi * f * i / fs);
        im += x[i] * std::sin(2 * kPi * f * i / fs);
    }
    return 2.0 * std::sqrt(re * re + im * im) / static_cast<double>(to - from);
}

struct Result {
    double noiseReductionDb; // in the pauses
    double toneChangeDb[3];  // in the phrases
};

static Result measure(const std::vector<float>& clean, const std::vector<float>& noisy, const std::vector<float>& out,
                      double fs, size_t L, double fromSec)
{
    Result r {};
    // noise in pauses: 0.62..0.93 s of each cycle (away from phrase edges)
    double inN = 0, outN = 0;
    for (double c = fromSec; c + 1.0 <= noisy.size() / fs - 0.1; c += 1.0) {
        const size_t a = static_cast<size_t>((c + 0.62) * fs), b = static_cast<size_t>((c + 0.93) * fs);
        for (size_t i = a; i < b; ++i) {
            inN += noisy[i] * noisy[i];
            outN += out[i + L] * out[i + L];
        }
    }
    r.noiseReductionDb = 10.0 * std::log10(inN / std::max(outN, 1e-30));
    // tones in the middle of phrases
    const double fr[3] = { 330, 990, 2310 };
    for (int k = 0; k < 3; ++k) {
        double a = 0, b = 0;
        for (double c = fromSec; c + 1.0 <= noisy.size() / fs - 0.1; c += 1.0) {
            const size_t s = static_cast<size_t>((c + 0.15) * fs), e = static_cast<size_t>((c + 0.40) * fs);
            std::vector<float> shifted(out.begin() + L, out.end());
            a += tone(clean, fs, fr[k], s, e);
            b += tone(shifted, fs, fr[k], s, e);
        }
        r.toneChangeDb[k] = 20.0 * std::log10(b / a);
    }
    return r;
}

static std::vector<float> run(SpectralDenoise& d, const std::vector<float>& x, int block = 512)
{
    return runMono(d, x, block);
}

static void testLearnedProfile()
{
    std::printf("learned profile: learn on 1 s of hiss, then clean the phrases\n");
    const double fs = 48000.0;
    auto clean = phrases(fs, 8.0);
    auto hiss = noise(clean.size() + static_cast<size_t>(fs), 0.01, 1);
    // first second: noise only (for learning), then phrases + noise
    std::vector<float> x(hiss.size()), cleanAll(hiss.size(), 0.0f);
    for (size_t i = 0; i < x.size(); ++i) {
        const size_t j = i >= static_cast<size_t>(fs) ? i - static_cast<size_t>(fs) : SIZE_MAX;
        const float c = j < clean.size() ? clean[j] : 0.0f;
        cleanAll[i] = c;
        x[i] = c + hiss[i];
    }

    SpectralDenoise d;
    d.prepare(fs, 1);
    SpectralDenoise::Settings s;
    s.adaptive = false;
    s.learn = true;
    d.setSettings(s);
    std::vector<float> out(x.size());
    const size_t learnEnd = static_cast<size_t>(fs * 0.9);
    for (size_t p = 0; p < x.size(); p += 512) {
        if (p >= learnEnd && s.learn) {
            s.learn = false;
            d.setSettings(s);
        }
        const int m = static_cast<int>(std::min<size_t>(512, x.size() - p));
        const float* in[1] = { x.data() + p };
        float* o[1] = { out.data() + p };
        d.process(in, o, 1, m);
    }
    CHECK(d.hasLearnedProfile(), "no profile learned");
    // shift measurement window by the 1 s lead-in
    std::vector<float> c2(cleanAll.begin() + static_cast<long>(fs), cleanAll.end());
    std::vector<float> x2(x.begin() + static_cast<long>(fs), x.end());
    std::vector<float> o2(out.begin() + static_cast<long>(fs), out.end());
    const Result r = measure(c2, x2, o2, fs, d.latency(), 1.0);
    std::printf("  noise in pauses reduced by %.1f dB (setting 12 dB)\n", r.noiseReductionDb);
    std::printf("  tones changed by %+.2f / %+.2f / %+.2f dB\n", r.toneChangeDb[0], r.toneChangeDb[1], r.toneChangeDb[2]);
    CHECK(r.noiseReductionDb > 10.0, "only %.1f dB noise reduction", r.noiseReductionDb);
    for (double c : r.toneChangeDb)
        CHECK(std::abs(c) < 1.0, "tone changed by %.2f dB", c);
}

static void testAdaptive()
{
    std::printf("adaptive: no learning, noise floor tracked automatically\n");
    const double fs = 44100.0;
    auto clean = phrases(fs, 10.0);
    auto hiss = noise(clean.size(), 0.01, 2);
    std::vector<float> x(clean.size());
    for (size_t i = 0; i < x.size(); ++i)
        x[i] = clean[i] + hiss[i];
    SpectralDenoise d;
    d.prepare(fs, 1);
    SpectralDenoise::Settings s;
    s.reductionDb = 20.0;
    d.setSettings(s);
    auto out = run(d, x);
    const Result r = measure(clean, x, out, fs, d.latency(), 3.0);
    std::printf("  noise in pauses reduced by %.1f dB (setting 20 dB)\n", r.noiseReductionDb);
    std::printf("  tones changed by %+.2f / %+.2f / %+.2f dB\n", r.toneChangeDb[0], r.toneChangeDb[1], r.toneChangeDb[2]);
    CHECK(r.noiseReductionDb > 15.0, "only %.1f dB noise reduction", r.noiseReductionDb);
    for (double c : r.toneChangeDb)
        CHECK(std::abs(c) < 1.0, "tone changed by %.2f dB", c);
}

static void testColouredNoise()
{
    std::printf("adaptive on coloured (low-heavy) noise, like a fan\n");
    const double fs = 48000.0;
    auto clean = phrases(fs, 10.0);
    auto w = noise(clean.size(), 0.02, 3);
    std::vector<float> x(clean.size());
    double lp = 0.0;
    for (size_t i = 0; i < x.size(); ++i) {
        lp = 0.97 * lp + 0.03 * w[i] * 4.0; // brown-ish
        x[i] = clean[i] + static_cast<float>(lp + 0.15 * w[i]);
    }
    SpectralDenoise d;
    d.prepare(fs, 1);
    SpectralDenoise::Settings s;
    s.reductionDb = 20.0;
    d.setSettings(s);
    auto out = run(d, x);
    const Result r = measure(clean, x, out, fs, d.latency(), 3.0);
    std::printf("  noise reduced by %.1f dB; tones %+.2f / %+.2f / %+.2f dB\n", r.noiseReductionDb, r.toneChangeDb[0],
                r.toneChangeDb[1], r.toneChangeDb[2]);
    CHECK(r.noiseReductionDb > 12.0, "only %.1f dB", r.noiseReductionDb);
    for (double c : r.toneChangeDb)
        CHECK(std::abs(c) < 1.0, "tone changed by %.2f dB", c);
}

static void testPassThroughWithoutProfile()
{
    std::printf("no profile and adaptive off: audio passes through unchanged\n");
    const double fs = 48000.0;
    auto x = musicLike(fs, 2.0, 4);
    SpectralDenoise d;
    d.prepare(fs, 1);
    SpectralDenoise::Settings s;
    s.adaptive = false;
    d.setSettings(s);
    auto out = run(d, x, 300);
    const size_t L = d.latency();
    const double err = rmsDiff(x, out, 0, x.size() - L, L);
    std::printf("  rms difference %.2e\n", err);
    CHECK(err < 1e-6, "changed audio by %.2e", err);
}

static void testProfileRoundTrip()
{
    std::printf("profile survives save/load, including at another sample rate\n");
    const double fs = 48000.0;
    auto hiss = noise(static_cast<size_t>(fs * 2), 0.01, 5);
    SpectralDenoise a;
    a.prepare(fs, 1);
    SpectralDenoise::Settings s;
    s.adaptive = false;
    s.learn = true;
    a.setSettings(s);
    run(a, hiss);
    s.learn = false;
    a.setSettings(s);
    std::vector<float> one(64, 0.0f);
    run(a, one);
    CHECK(a.hasLearnedProfile(), "no profile");
    const std::string text = a.profileToString();
    std::printf("  profile text: %zu bytes\n", text.size());

    SpectralDenoise b;
    b.prepare(fs, 1);
    b.setSettings(s);
    CHECK(b.profileFromString(text), "same-rate load failed");
    b.setSettings(s);
    auto ea = a.currentNoiseEstimate(), eb = b.currentNoiseEstimate();
    double worst = 0.0;
    for (size_t k = 1; k < ea.size(); ++k)
        worst = std::max(worst, std::abs(10.0 * std::log10(eb[k] / ea[k])));
    std::printf("  same rate: worst bin difference %.3f dB\n", worst);
    CHECK(worst < 0.01, "same-rate mismatch %.3f dB", worst);

    SpectralDenoise c;
    c.prepare(96000.0, 1);
    c.setSettings(s);
    CHECK(c.profileFromString(text), "96 kHz load failed");
    // compare average level over 1-10 kHz (white noise: flat)
    auto ec = c.currentNoiseEstimate();
    auto avg = [](const std::vector<double>& e, double hzPerBin) {
        double sum = 0;
        int n = 0;
        for (size_t k = 0; k < e.size(); ++k)
            if (k * hzPerBin > 1000 && k * hzPerBin < 10000) {
                sum += e[k];
                ++n;
            }
        return sum / n;
    };
    // 96 kHz with twice the FFT size and half the per-sample noise power per
    // bin... the same physical noise is what matters: compare against a
    // profile learned directly at 96 kHz from the same noise level.
    SpectralDenoise ref;
    ref.prepare(96000.0, 1);
    ref.setSettings({ 0, 12, 50, false, true, false });
    run(ref, noise(96000 * 2, 0.01, 6));
    ref.setSettings(s);
    run(ref, one);
    const double diff = 10.0 * std::log10(avg(ec, 96000.0 / c.fftSize()) / avg(ref.currentNoiseEstimate(), 96000.0 / ref.fftSize()));
    std::printf("  48k profile loaded at 96k vs learned at 96k: %+.2f dB\n", diff);
    CHECK(std::abs(diff) < 0.5, "resampled profile off by %.2f dB", diff);

    SpectralDenoise bad;
    bad.prepare(fs, 1);
    CHECK(!bad.profileFromString("garbage 1 2"), "accepted garbage");
    CHECK(!bad.hasLearnedProfile(), "garbage created a profile");
}

static void testNoiseOnlyIsComplement()
{
    std::printf("noise-only output plus cleaned output equals the (delayed) input\n");
    const double fs = 48000.0;
    auto clean = phrases(fs, 4.0);
    auto hiss = noise(clean.size(), 0.01, 7);
    std::vector<float> x(clean.size());
    for (size_t i = 0; i < x.size(); ++i)
        x[i] = clean[i] + hiss[i];
    SpectralDenoise a, b;
    a.prepare(fs, 1);
    b.prepare(fs, 1);
    SpectralDenoise::Settings s;
    a.setSettings(s);
    s.noiseOnly = true;
    b.setSettings(s);
    auto ya = run(a, x), yb = run(b, x);
    const size_t L = a.latency();
    double worst = 0.0;
    for (size_t i = 0; i + L < x.size(); ++i)
        worst = std::max(worst, std::abs(static_cast<double>(x[i]) - ya[i + L] - yb[i + L]));
    std::printf("  max error %.2e\n", worst);
    CHECK(worst < 1e-5, "complement error %.2e", worst);
}

int main()
{
    testLearnedProfile();
    testAdaptive();
    testColouredNoise();
    testPassThroughWithoutProfile();
    testProfileRoundTrip();
    testNoiseOnlyIsComplement();
    return finish();
}
