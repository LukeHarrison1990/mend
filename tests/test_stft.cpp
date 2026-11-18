// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#include "TestUtil.hpp"
#include "mend/Fft.hpp"
#include "mend/Stft.hpp"

using namespace mend;
using namespace mendtest;

struct Identity : Stft::FrameProcessor {
    void processFrame(std::complex<double>*, int, int) override {}
};

struct BrickLowpass : Stft::FrameProcessor {
    int cutoffBin = 0;
    void processFrame(std::complex<double>* bins, int n, int) override
    {
        for (int i = cutoffBin; i < n; ++i)
            bins[i] = 0.0;
    }
};

static void testFftRoundTrip()
{
    std::printf("FFT round trip and known spectrum\n");
    Fft fft;
    const int n = 1024;
    fft.prepare(n);
    std::vector<double> x(n), y(n);
    for (int i = 0; i < n; ++i)
        x[i] = std::sin(2.0 * kPi * 8.0 * i / n) + 0.25 * std::cos(2.0 * kPi * 100.0 * i / n);
    std::vector<std::complex<double>> bins(n / 2 + 1), scratch(n);
    fft.forwardReal(x.data(), bins.data(), scratch.data());
    CHECK(std::abs(std::abs(bins[8]) - n / 2.0) < 1e-6, "bin 8 magnitude %.6f", std::abs(bins[8]));
    CHECK(std::abs(std::abs(bins[100]) - n / 8.0) < 1e-6, "bin 100 magnitude %.6f", std::abs(bins[100]));
    CHECK(std::abs(bins[50]) < 1e-9, "bin 50 should be empty");
    fft.inverseReal(bins.data(), y.data(), scratch.data());
    double err = 0.0;
    for (int i = 0; i < n; ++i)
        err = std::max(err, std::abs(x[i] - y[i]));
    std::printf("  max round-trip error %.2e\n", err);
    CHECK(err < 1e-12, "round trip error %.2e", err);
}

static void testStftIdentity(int fftSize, int overlap)
{
    std::printf("STFT identity: size %d, overlap %dx\n", fftSize, overlap);
    const double fs = 48000.0;
    auto x = musicLike(fs, 1.0, 7);
    Stft stft;
    stft.prepare(fftSize, overlap, 1);
    Identity id;
    std::vector<float> out(x.size());
    for (size_t pos = 0; pos < x.size(); pos += 300) { // odd host block size on purpose
        const int n = static_cast<int>(std::min<size_t>(300, x.size() - pos));
        const float* in[1] = { x.data() + pos };
        float* o[1] = { out.data() + pos };
        stft.process(in, o, 1, n, id);
    }
    const size_t L = stft.latency();
    const double err = rmsDiff(x, out, fftSize, x.size() - L, L);
    std::printf("  reconstruction error %.2e (signal rms %.3f)\n", err, rms(x, 0, x.size()));
    CHECK(err < 1e-6, "reconstruction error %.2e", err);
}

static void testStftLowpass()
{
    std::printf("STFT brick-wall low-pass removes a high tone, keeps a low one\n");
    const double fs = 48000.0;
    const int n = 2048;
    std::vector<float> x(static_cast<size_t>(fs));
    for (size_t i = 0; i < x.size(); ++i)
        x[i] = static_cast<float>(0.3 * std::sin(2.0 * kPi * 300.0 * i / fs) + 0.3 * std::sin(2.0 * kPi * 6000.0 * i / fs));
    Stft stft;
    stft.prepare(n, 4, 1);
    BrickLowpass lp;
    lp.cutoffBin = static_cast<int>(2000.0 / fs * n);
    std::vector<float> out(x.size());
    const float* in[1] = { x.data() };
    float* o[1] = { out.data() };
    stft.process(in, o, 1, static_cast<int>(x.size()), lp);

    auto amp = [&](const std::vector<float>& s, double f, size_t from, size_t to) {
        double re = 0, im = 0;
        for (size_t i = from; i < to; ++i) {
            re += s[i] * std::cos(2.0 * kPi * f * i / fs);
            im += s[i] * std::sin(2.0 * kPi * f * i / fs);
        }
        return 2.0 * std::sqrt(re * re + im * im) / (to - from);
    };
    const double low = amp(out, 300.0, 10000, 40000), high = amp(out, 6000.0, 10000, 40000);
    std::printf("  300 Hz: %.4f (was 0.3), 6 kHz: %.2e\n", low, high);
    CHECK(std::abs(low - 0.3) < 0.003, "low tone changed to %.4f", low);
    CHECK(high < 1e-4, "high tone still at %.2e", high);
}

int main()
{
    testFftRoundTrip();
    testStftIdentity(1024, 4);
    testStftIdentity(2048, 2);
    testStftIdentity(4096, 8);
    testStftLowpass();
    return finish();
}
