// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
// Tests for the editor engine: files, undo, and offline chain rendering.
#include "TestUtil.hpp"

#include "Document.hpp"
#include "Renderer.hpp"
#include "Tools.hpp"
#include "Spectrogram.hpp"
#include "Modules.hpp"

#include <cstdio>

using namespace mend;
using namespace mend::editor;
using namespace mendtest;

static AudioBuffer stereo(const std::vector<float>& l, const std::vector<float>& r, double fs)
{
    AudioBuffer b;
    b.sampleRate = fs;
    b.channels = { l, r };
    return b;
}

static double tone(const std::vector<float>& x, double fs, double f, size_t a, size_t b)
{
    double re = 0, im = 0;
    for (size_t i = a; i < b; ++i) {
        re += x[i] * std::cos(2 * kPi * f * i / fs);
        im += x[i] * std::sin(2 * kPi * f * i / fs);
    }
    return 2.0 * std::sqrt(re * re + im * im) / static_cast<double>(b - a);
}

static void testWavRoundTrip()
{
    std::printf("WAV export and re-import (24-bit and float)\n");
    const double fs = 44100.0;
    auto x = musicLike(fs, 1.0, 1, 0.5);
    auto buf = stereo(x, x, fs);
    for (auto fmt : { WavFormat::Int24, WavFormat::Float32 }) {
        const char* path = fmt == WavFormat::Int24 ? "/tmp/mend_test24.wav" : "/tmp/mend_testf.wav";
        std::string err;
        CHECK(saveWavFile(path, buf, fmt, err), "save failed: %s", err.c_str());
        AudioBuffer back;
        CHECK(loadAudioFile(path, back, err), "load failed: %s", err.c_str());
        CHECK(back.numChannels() == 2 && back.numFrames() == x.size(), "shape changed");
        CHECK(std::abs(back.sampleRate - fs) < 0.5, "rate changed");
        double worst = 0;
        for (size_t i = 0; i < x.size() && i < back.numFrames(); ++i)
            worst = std::max(worst, std::abs(static_cast<double>(back.channels[1][i]) - x[i]));
        std::printf("  %s: worst sample error %.2e\n", fmt == WavFormat::Int24 ? "24-bit" : "float", worst);
        CHECK(worst < (fmt == WavFormat::Int24 ? 2e-7 : 1e-9), "error %.2e", worst);
    }
    AudioBuffer none;
    std::string err;
    CHECK(!loadAudioFile("/tmp/does-not-exist.wav", none, err) && !err.empty(), "missing file should fail clearly");
}

static void testUndoRedo()
{
    std::printf("undo and redo of a rendered region\n");
    std::string err;
    auto x = musicLike(48000.0, 1.0, 2);
    saveWavFile("/tmp/mend_undo.wav", stereo(x, x, 48000.0), WavFormat::Float32, err);
    Document d;
    CHECK(d.load("/tmp/mend_undo.wav", err), "load failed");
    const auto original = d.audio().channels[0];
    std::vector<std::vector<float>> zeros(2, std::vector<float>(1000, 0.0f));
    d.replaceRegion(5000, zeros, "Test");
    CHECK(d.audio().channels[0][5500] == 0.0f && d.canUndo() && d.modified(), "edit not applied");
    d.undo();
    CHECK(d.audio().channels[0] == original && d.canRedo(), "undo did not restore");
    d.redo();
    CHECK(d.audio().channels[1][5999] == 0.0f, "redo did not reapply");
    CHECK(d.undoLabel() == "Test", "label lost");
}

static void testChainAlignment()
{
    std::printf("chain of latency-carrying tools stays sample-aligned\n");
    const double fs = 48000.0;
    auto x = musicLike(fs, 4.0, 3, 0.4);
    auto buf = stereo(x, x, fs);
    ChainSettings chain = defaultChain();
    chain[static_cast<int>(ToolId::DeClick)].enabled = true;  // ~43 ms latency
    chain[static_cast<int>(ToolId::DeClip)].enabled = true;   // ~85 ms latency
    auto out = Renderer::renderRegion(buf, 0, x.size(), chain, nullptr, nullptr, nullptr, nullptr);
    CHECK(out.size() == 2 && out[0].size() == x.size(), "wrong output shape");
    // clean music: these tools should leave it (almost) alone, *and in time*
    const double diff = rmsDiff(x, out[0], 4800, x.size() - 4800);
    const double level = rms(x, 4800, x.size() - 4800);
    std::printf("  output vs input: %.1f dB difference\n", 20.0 * std::log10(diff / level));
    CHECK(diff < 0.03 * level, "output misaligned or changed: %.1f dB", 20.0 * std::log10(diff / level));

    // De-plosive's crossover shifts low-end phase (inaudible, like any
    // multiband tool), so check its timing and level rather than identity.
    chain[static_cast<int>(ToolId::DePlosive)].enabled = true;
    out = Renderer::renderRegion(buf, 0, x.size(), chain, nullptr, nullptr, nullptr, nullptr);
    // compare above 1 kHz, where the crossover adds no delay
    auto highpass = [&](const std::vector<float>& v) {
        Biquad a, b;
        a.setCoeffs(BiquadCoeffs::highpass(fs, 1000.0, 0.7071));
        b.setCoeffs(BiquadCoeffs::highpass(fs, 1000.0, 0.7071));
        std::vector<float> y(v.size());
        for (size_t i = 0; i < v.size(); ++i)
            y[i] = static_cast<float>(b.process(a.process(v[i])));
        return y;
    };
    const auto xh = highpass(x), oh = highpass(out[0]);
    int bestLag = 0;
    double best = -1e30;
    for (int lag = -200; lag <= 200; ++lag) {
        double c = 0;
        for (size_t i = 10000; i < 60000; ++i)
            c += static_cast<double>(xh[i]) * oh[i + lag];
        if (c > best) {
            best = c;
            bestLag = lag;
        }
    }
    const double lvl = 20.0 * std::log10(rms(out[0], 4800, x.size() - 4800) / level);
    std::printf("  with De-plosive too: content above 1 kHz aligned at lag %d samples, level %+.2f dB\n", bestLag, lvl);
    CHECK(std::abs(bestLag) <= 2, "chain output shifted by %d samples", bestLag);
    CHECK(std::abs(lvl) < 0.3, "level changed %.2f dB", lvl);
}

static void testDeHumFromFirstSecond()
{
    std::printf("offline De-hum removes hum from the very first second\n");
    const double fs = 44100.0;
    auto x = musicLike(fs, 6.0, 4, 0.3);
    std::mt19937 rng(5);
    std::normal_distribution<double> n(0.0, 0.003);
    std::vector<float> y(x.size());
    for (size_t i = 0; i < x.size(); ++i) {
        double h = 0;
        for (int k = 1; k <= 5; ++k)
            h += 0.04 / k * std::sin(2 * kPi * 50.15 * k * i / fs + k);
        y[i] = static_cast<float>(x[i] + h + n(rng));
    }
    auto buf = stereo(y, y, fs);
    ChainSettings chain = defaultChain();
    chain[static_cast<int>(ToolId::DeHum)].enabled = true;
    auto out = Renderer::renderRegion(buf, 0, y.size(), chain, nullptr, nullptr, nullptr, nullptr);
    const size_t a = static_cast<size_t>(0.1 * fs), b = static_cast<size_t>(1.0 * fs);
    for (int k = 1; k <= 3; ++k) {
        const double red = 20.0 * std::log10(tone(y, fs, 50.15 * k, a, b) / tone(out[0], fs, 50.15 * k, a, b));
        std::printf("  harmonic %d in the first second: -%.1f dB\n", k, red);
        CHECK(red > 20.0, "harmonic %d only reduced %.1f dB in the first second", k, red);
    }
    const double keep = 20.0 * std::log10(tone(out[0], fs, 440.0, a, b) / tone(y, fs, 440.0, a, b));
    std::printf("  440 Hz note: %+.2f dB\n", keep);
    CHECK(std::abs(keep) < 0.5, "note changed %.2f dB", keep);
}

static void testPartialRegion()
{
    std::printf("rendering a selection leaves everything outside it untouched\n");
    const double fs = 48000.0;
    auto x = musicLike(fs, 5.0, 6, 0.3);
    std::mt19937 rng(9);
    std::normal_distribution<double> n(0.0, 0.01);
    for (auto& v : x)
        v += static_cast<float>(n(rng));
    auto buf = stereo(x, x, fs);
    ChainSettings chain = defaultChain();
    chain[static_cast<int>(ToolId::DeNoise)].enabled = true;
    const size_t s = static_cast<size_t>(2.0 * fs), len = static_cast<size_t>(1.5 * fs);
    auto out = Renderer::renderRegion(buf, s, len, chain, nullptr, nullptr, nullptr, nullptr);
    CHECK(out[0].size() == len, "wrong region length");
    Document d;
    std::string err;
    saveWavFile("/tmp/mend_part.wav", buf, WavFormat::Float32, err);
    d.load("/tmp/mend_part.wav", err);
    d.replaceRegion(s, out, "De-noise");
    const auto& r = d.audio().channels[0];
    bool outsideSame = true;
    for (size_t i = 0; i < s; ++i)
        outsideSame &= r[i] == x[i];
    for (size_t i = s + len; i < x.size(); ++i)
        outsideSame &= r[i] == x[i];
    CHECK(outsideSame, "audio outside the selection changed");
    const double inside = 20.0 * std::log10(rms(r, s + 4800, s + len - 4800) / rms(x, s + 4800, s + len - 4800));
    std::printf("  inside the selection: %+.1f dB (noise reduced), outside: unchanged\n", inside);
    CHECK(inside < -0.5, "selection not processed");
    // the edge is a smooth crossfade, not a jump
    double jump = 0;
    for (size_t i = s - 2; i < s + 2; ++i)
        jump = std::max(jump, static_cast<double>(std::abs(r[i + 1] - r[i])));
    CHECK(jump < 0.1, "discontinuity %.3f at the selection edge", jump);
}

static void testLearnProfile()
{
    std::printf("De-noise profile learned from a selection\n");
    std::mt19937 rng(2);
    std::normal_distribution<double> n(0.0, 0.01);
    std::vector<float> hiss(48000);
    for (auto& v : hiss)
        v = static_cast<float>(n(rng));
    const std::string p = learnNoiseProfile({ hiss, hiss }, 48000.0);
    CHECK(!p.empty() && p.rfind("MENDNP1", 0) == 0, "no profile learned");
    CHECK(learnNoiseProfile({ std::vector<float>(100, 0.0f) }, 48000.0).empty(), "learned from 100 samples");
}

static void testSpectrogram()
{
    std::printf("Spectrogram puts a tone at its frequency, waveform image spans its peaks\n");
    AudioBuffer a;
    a.sampleRate = 48000;
    a.channels.assign(1, std::vector<float>(48000));
    for (size_t i = 0; i < a.channels[0].size(); ++i)
        a.channels[0][i] = static_cast<float>(0.5 * std::sin(2 * 3.14159265358979 * 1000.0 * i / 48000.0));
    for (FreqScale sc : { FreqScale::Linear, FreqScale::Mel, FreqScale::Log }) {
        SpecView v;
        v.cols = 64;
        v.rows = 400;
        v.framesPerCol = 48000.0 / 64;
        v.scale = sc;
        std::vector<uint8_t> img;
        renderSpectrogram(a, 0, v, img);
        // brightest row in the middle column
        int best = 0, bestV = -1;
        for (int r = 0; r < v.rows; ++r) {
            const uint8_t* p = img.data() + (static_cast<size_t>(r) * v.cols + 32) * 4;
            const int lum = p[0] + p[1] + p[2];
            if (lum > bestV) {
                bestV = lum;
                best = r;
            }
        }
        const double hz = specFreqAt(sc, 48000, (best + 0.5) / v.rows);
        CHECK(std::abs(hz - 1000.0) < 60.0, "peak at %.0f Hz instead of 1000 (scale %d)", hz, static_cast<int>(sc));
        CHECK(std::abs(specPosOf(sc, 48000, specFreqAt(sc, 48000, 0.3)) - 0.3) < 1e-9, "axis mapping not invertible");
    }
    PeakCache pc;
    pc.rebuild(a);
    std::vector<uint8_t> w;
    rasterWaveform(a, pc, 0, 0.0, 48000.0 / 100, 100, 200, 255, 255, 255, w);
    int top = 200, bottom = -1;
    for (int y = 0; y < 200; ++y)
        if (w[(static_cast<size_t>(y) * 100 + 50) * 4 + 3] > 128) {
            top = std::min(top, y);
            bottom = std::max(bottom, y);
        }
    // amplitude 0.5 of a 0.47*h half-height -> about 100 +/- 47 pixels
    CHECK(std::abs(top - 53) <= 2 && std::abs(bottom - 146) <= 2, "waveform spans rows %d..%d", top, bottom);
}

static void testModules()
{
    std::printf("Modules: region edits, whole-file edits and undo\n");
    Document doc;
    {
        AudioBuffer a;
        a.sampleRate = 48000;
        a.channels.assign(2, std::vector<float>(48000 * 3));
        for (size_t i = 0; i < a.channels[0].size(); ++i) {
            const float v = (i / 48000) == 1 ? 0.0f : static_cast<float>(0.3 * std::sin(2 * 3.14159265 * 440.0 * i / 48000.0));
            a.channels[0][i] = a.channels[1][i] = v;
        }
        const std::string path = "/tmp/mend_test_modules.wav";
        std::string err;
        CHECK(saveWavFile(path, a, WavFormat::Float32, err), "couldn't write the test file: %s", err.c_str());
        CHECK(doc.load(path, err), "couldn't load: %s", err.c_str());
    }
    auto run = [&](ModuleId id, int action, size_t s, size_t n, bool partial, std::vector<float> values, ModuleOutput& out,
                   std::string& err) {
        ModuleInput in;
        in.audio = &doc.audio();
        in.start = s;
        in.length = n;
        in.partial = partial;
        in.values = values.empty() ? defaultModuleValues(id) : values;
        return runModule(id, action, in, out, err);
    };
    ModuleOutput out;
    std::string err;
    // gain on a partial selection: -6 dB inside, joined smoothly at the edges
    CHECK(run(ModuleId::Gain, 0, 12000, 12000, true, { -6.0f }, out, err), "gain failed: %s", err.c_str());
    CHECK(out.kind == ModuleOutput::Kind::Region && out.start == 12000 && out.region[0].size() == 12000, "gain region");
    const float mid = out.region[0][6000], orig = doc.audio().channels[0][18000];
    CHECK(std::abs(mid - orig * 0.501f) < 1e-3f, "gain inside %.4f vs %.4f", mid, orig * 0.501f);
    CHECK(std::abs(out.region[0][0] - doc.audio().channels[0][12000]) < 1e-3f, "gain edge not joined");
    doc.replaceRegion(out.start, std::move(out.region), out.label);

    // a stereo-only module refuses a selection-only module without a selection
    out = {};
    CHECK(!run(ModuleId::Fade, 0, 0, doc.audio().numFrames(), false, {}, out, err), "fade ran without a selection");

    // trim silence: the 1 s gap in the middle shrinks to 0.5 s
    out = {};
    CHECK(run(ModuleId::TrimSilence, 0, 0, doc.audio().numFrames(), false, {}, out, err), "trim failed: %s", err.c_str());
    CHECK(out.kind == ModuleOutput::Kind::Whole, "trim kind");
    const size_t before = doc.audio().numFrames();
    doc.replaceAll(std::move(out.whole), out.label);
    const double removed = (before - doc.audio().numFrames()) / 48000.0;
    CHECK(std::abs(removed - 0.5) < 0.03, "trim removed %.3f s", removed);

    // resample, then undo both whole-file changes
    out = {};
    CHECK(run(ModuleId::Resample, 0, 0, doc.audio().numFrames(), false, { 2.0f }, out, err), "resample failed: %s", err.c_str());
    doc.replaceAll(std::move(out.whole), out.label);
    CHECK(doc.audio().sampleRate == 44100 && std::abs(doc.audio().seconds() - 2.5) < 0.01, "resampled to %.0f Hz, %.3f s",
          doc.audio().sampleRate, doc.audio().seconds());
    doc.undo();
    doc.undo();
    CHECK(doc.audio().sampleRate == 48000 && doc.audio().numFrames() == before, "undo didn't restore the original");
    doc.redo();
    CHECK(std::abs(doc.audio().seconds() - 2.5) < 0.01, "redo didn't re-trim");

    // measuring actions don't edit
    out = {};
    CHECK(run(ModuleId::Loudness, 1, 0, doc.audio().numFrames(), false, {}, out, err), "loudness measure failed");
    CHECK(out.kind == ModuleOutput::Kind::None && !out.info.empty(), "measure produced an edit");
    out = {};
    CHECK(!run(ModuleId::Interpolate, 0, 1000, 48000, true, {}, out, err), "interpolate accepted a 1 s selection");
}

int main()
{
    testWavRoundTrip();
    testUndoRedo();
    testChainAlignment();
    testDeHumFromFirstSecond();
    testPartialRegion();
    testLearnProfile();
    testSpectrogram();
    testModules();
    return finish();
}
