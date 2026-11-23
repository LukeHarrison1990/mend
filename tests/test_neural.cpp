// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
// Voice (neural) noise reduction: DeepFilterNet3 inside the De-noise engine.
#include "TestUtil.hpp"
#include "mend/Biquad.hpp"
#include "mend/DeNoiseEngine.hpp"
#include "mend/Offline.hpp"

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


} // namespace

namespace {
// A short sentence of synthetic speech (CMU Flite, "slt" voice), 16 kHz.
std::vector<float> speech(double fs)
{
    std::vector<float> x;
    if (std::FILE* f = std::fopen(MEND_TEST_DATA "/speech16k.raw", "rb")) {
        std::int16_t b[4096];
        size_t n;
        while ((n = std::fread(b, 2, 4096, f)) > 0)
            for (size_t i = 0; i < n; ++i)
                x.push_back(b[i] / 32768.0f * 0.25f); // about -26 dBFS RMS
        std::fclose(f);
    }
    CHECK(!x.empty(), "couldn't read %s", MEND_TEST_DATA "/speech16k.raw");
    if (x.empty())
        x.assign(16000, 0.0f);
    return offline::resample({ x }, 16000.0, fs)[0];
}

double energy(const std::vector<float>& x, size_t from, size_t to, const std::vector<float>* minus = nullptr, size_t shift = 0)
{
    double s = 0.0;
    for (size_t i = from; i < to && i + shift < x.size(); ++i) {
        const double v = x[i + shift] - (minus ? (*minus)[i] : 0.0f);
        s += v * v;
    }
    return s;
}

struct Result {
    double inSnr, outSnr;
};

// Mix speech-like sound with white noise at `snrDb`, run Voice mode, and
// measure the SNR before and after against the clean signal (aligned by
// the reported latency).
Result run(double fs, int block, double snrDb, float reductionDb)
{
    const std::vector<float> clean = speech(fs);
    std::vector<float> noisy = clean;
    Lcg rng { 99 };
    const double cs = energy(clean, 0, clean.size()) / clean.size();
    const double g = std::sqrt(cs / std::pow(10.0, snrDb / 10.0) * 3.0); // uniform noise has variance 1/3
    for (auto& v : noisy)
        v += static_cast<float>(g * rng.next());
    DeNoiseEngine e;
    e.prepare(fs, 1, block);
    e.setMode(DeNoiseEngine::Mode::Voice);
    e.setVoiceReductionDb(reductionDb);
    const int L = e.latency();
    std::vector<float> in = noisy;
    in.resize(noisy.size() + static_cast<size_t>(L), 0.0f);
    const std::vector<float> out = runMono(e, in, block);
    const size_t from = static_cast<size_t>(fs * 2.0), to = clean.size(); // after the model settles
    const double c = energy(clean, from, to);
    Result r;
    r.inSnr = 10.0 * std::log10(c / energy(noisy, from, to, &clean));
    r.outSnr = 10.0 * std::log10(c / energy(out, from, to, &clean, static_cast<size_t>(L)));
    return r;
}
} // namespace

int main()
{
    std::printf("weights\n");
    CHECK(NeuralDenoise::available(), "model weights missing or the wrong size");

    for (double fs : { 48000.0, 44100.0, 96000.0 }) {
        std::printf("voice mode at %.0f Hz\n", fs);
        const Result r = run(fs, fs == 44100.0 ? 333 : 512, 5.0, 60.0f);
        std::printf("  SNR %.1f dB -> %.1f dB\n", r.inSnr, r.outSnr);
        CHECK(r.outSnr > r.inSnr + 8.0, "voice mode improved SNR by only %.1f dB at %.0f Hz", r.outSnr - r.inSnr, fs);
    }

    std::printf("reduction limit\n");
    {
        const Result full = run(48000.0, 512, 5.0, 60.0f), lim = run(48000.0, 512, 5.0, 6.0f);
        std::printf("  full %.1f dB, limited to 6 dB: %.1f dB\n", full.outSnr, lim.outSnr);
        CHECK(lim.outSnr < full.outSnr, "a 6 dB limit should remove less noise");
        CHECK(lim.outSnr > lim.inSnr + 2.0 && lim.outSnr < lim.inSnr + 7.0, "6 dB limit gave %.1f dB",
              lim.outSnr - lim.inSnr);
    }

    std::printf("silence and switching modes\n");
    {
        const double fs = 48000.0;
        DeNoiseEngine e;
        e.prepare(fs, 2, 256);
        const int L = e.latency();
        std::vector<float> x(static_cast<size_t>(fs * 3), 0.0f);
        // a steady tone at -20 dBFS: neither engine should click when the mode changes
        for (size_t i = 0; i < x.size(); ++i)
            x[i] = 0.1f * static_cast<float>(std::sin(2.0 * 3.14159265358979 * 220.0 * i / fs));
        std::vector<float> l(x.size()), r(x.size());
        bool finite = true;
        for (size_t pos = 0; pos < x.size(); pos += 256) {
            if (pos == static_cast<size_t>(fs))
                e.setMode(DeNoiseEngine::Mode::Voice);
            if (pos == static_cast<size_t>(fs * 2))
                e.setMode(DeNoiseEngine::Mode::AnySound);
            CHECK(e.latency() == L, "latency changed with the mode");
            const int n = static_cast<int>(std::min<size_t>(256, x.size() - pos));
            const float* in[2] = { x.data() + pos, x.data() + pos };
            float* out[2] = { l.data() + pos, r.data() + pos };
            e.process(in, out, 2, n);
        }
        float maxStep = 0.0f;
        for (size_t i = 1; i < l.size(); ++i) {
            finite = finite && std::isfinite(l[i]) && std::isfinite(r[i]);
            maxStep = std::max(maxStep, std::fabs(l[i] - l[i - 1]));
        }
        CHECK(finite, "non-finite output");
        // a 220 Hz sine at 0.1 moves at most 0.0029 per sample; allow some processing
        CHECK(maxStep < 0.02f, "jump of %.4f in the output around a mode switch", maxStep);

        std::vector<float> zeros(48000, 0.0f), o1(48000), o2(48000);
        e.reset();
        e.setMode(DeNoiseEngine::Mode::Voice);
        const float* in[2] = { zeros.data(), zeros.data() };
        float* out[2] = { o1.data(), o2.data() };
        e.process(in, out, 2, 48000);
        double s = 0.0;
        for (float v : o1)
            s += std::fabs(v);
        CHECK(s == 0.0, "digital silence in, not silence out (%g)", s);
    }
    return finish();
}
