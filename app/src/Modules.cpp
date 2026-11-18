// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#include "Modules.hpp"

#include "mend/Biquad.hpp"
#include "mend/Offline.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace mend::editor {

namespace off = mend::offline;
using K = ToolParam::Kind;
using Channels = std::vector<std::vector<float>>;

namespace {

const char* const kSpectralModes[] = { "Attenuate", "Replace", "Gain" };
const char* const kFadeDirs[] = { "Fade in", "Fade out" };
const char* const kFadeCurves[] = { "Linear", "Equal power", "S-curve", "Exponential", "Logarithmic" };
const char* const kMixOps[] = { "None", "Swap L/R", "Mono (sum)", "Encode mid/side", "Decode mid/side" };
const char* const kSignals[] = { "Silence", "Sine tone", "White noise", "Pink noise", "Bleep (1 kHz)" };
const char* const kRates[] = { "22.05 kHz", "32 kHz", "44.1 kHz", "48 kHz", "88.2 kHz", "96 kHz", "192 kHz" };
const double kRateValues[] = { 22050, 32000, 44100, 48000, 88200, 96000, 192000 };
const char* const kBits[] = { "16-bit", "24-bit", "32-bit float" };
const char* const kDither[] = { "None", "TPDF", "Noise-shaped" };

std::vector<ModuleInfo> makeInfos()
{
    std::vector<ModuleInfo> m(static_cast<size_t>(ModuleId::Count));
    auto set = [&](ModuleId id, ModuleInfo info) { m[static_cast<size_t>(id)] = std::move(info); };
    set(ModuleId::SpectralRepair,
        { "Spectral Repair", "Fixes what's inside a box drawn on the spectrogram",
          { { "Mode", 0, 2, 0, K::Choice, nullptr, kSpectralModes },
            { "Strength", 0, 100, 100, K::Knob, "%.0f%%" },
            { "Gain", -40, 12, -12, K::Knob, "%+.1f dB" } },
          "Repair", nullptr, true, false, false });
    set(ModuleId::Interpolate,
        { "Interpolate", "Rebuilds a short selection from the audio around it", {}, "Interpolate", nullptr, true, false,
          false });
    set(ModuleId::Gain, { "Gain", "Raises or lowers the level", { { "Gain", -40, 24, 0, K::Knob, "%+.1f dB" } }, "Apply",
                          nullptr, false, false, false });
    set(ModuleId::Normalize,
        { "Normalize", "Sets the peak level",
          { { "Per channel", 0, 1, 0, K::Toggle, nullptr },
            { "Remove DC offset", 0, 1, 1, K::Toggle, nullptr },
            { "Peak", -24, 0, -1, K::Knob, "%.1f dBFS" } },
          "Normalize", nullptr, false, false, false });
    set(ModuleId::Fade,
        { "Fade", "Fades the selection in or out",
          { { "Direction", 0, 1, 0, K::Choice, nullptr, kFadeDirs }, { "Curve", 0, 4, 1, K::Choice, nullptr, kFadeCurves } },
          "Fade", nullptr, true, false, false });
    set(ModuleId::Phase,
        { "Phase", "Polarity, DC offset and phase rotation",
          { { "Invert polarity", 0, 1, 0, K::Toggle, nullptr },
            { "Remove DC offset", 0, 1, 0, K::Toggle, nullptr },
            { "Rotation", -180, 180, 0, K::Knob, "%.0f deg" } },
          "Apply", "Suggest rotation", false, false, false });
    set(ModuleId::Mixing,
        { "Mixing", "Channel swaps, mono, mid/side and balance",
          { { "Operation", 0, 4, 0, K::Choice, nullptr, kMixOps },
            { "Left", -24, 12, 0, K::Knob, "%+.1f dB" },
            { "Right", -24, 12, 0, K::Knob, "%+.1f dB" } },
          "Apply", nullptr, false, true, false });
    set(ModuleId::Azimuth,
        { "Azimuth", "Lines up a late or quieter right channel",
          { { "Delay", -100, 100, 0, K::Knob, "%.2f smp" }, { "Gain", -12, 12, 0, K::Knob, "%+.2f dB" } }, "Apply",
          "Measure", false, true, false });
    set(ModuleId::SignalGenerator,
        { "Signal Generator", "Fills the selection with silence, a tone or noise",
          { { "Signal", 0, 4, 0, K::Choice, nullptr, kSignals },
            { "Frequency", 20, 20000, 1000, K::LogKnob, "%.0f Hz" },
            { "Level", -60, 0, -20, K::Knob, "%.1f dBFS" } },
          "Generate", nullptr, true, false, false });
    set(ModuleId::Loudness,
        { "Loudness", "Measures and sets loudness (BS.1770)",
          { { "True-peak limit", 0, 1, 1, K::Toggle, nullptr },
            { "Target", -36, -6, -23, K::Knob, "%.1f LUFS" },
            { "Ceiling", -6, 0, -1, K::Knob, "%.1f dBTP" } },
          "Apply", "Measure", false, false, false });
    set(ModuleId::EqMatch,
        { "EQ Match", "Gives one recording the tone of another",
          { { "Amount", 0, 100, 80, K::Knob, "%.0f%%" }, { "Max change", 0, 24, 12, K::Knob, "%.0f dB" } }, "Match",
          "Learn reference", false, false, false });
    set(ModuleId::TrimSilence,
        { "Trim Silence", "Shortens long gaps",
          { { "Threshold", -80, -20, -50, K::Knob, "%.0f dBFS" },
            { "Longer than", 0.1f, 5, 1, K::LogKnob, "%.2f s" },
            { "Keep", 0, 2, 0.5f, K::Knob, "%.2f s" } },
          "Trim", nullptr, false, false, false });
    set(ModuleId::Resample,
        { "Resample", "Converts the sample rate", { { "Rate", 0, 6, 3, K::Choice, nullptr, kRates } }, "Resample",
          nullptr, false, false, true });
    set(ModuleId::Stats,
        { "Waveform Stats", "Levels, loudness and spectrum of the selection", {}, "Analyse", nullptr, false, false,
          false });
    set(ModuleId::ExportFormat,
        { "Export Format", "Bit depth and dither for Export WAV",
          { { "Bit depth", 0, 2, 1, K::Choice, nullptr, kBits }, { "Dither", 0, 2, 1, K::Choice, nullptr, kDither } },
          nullptr, nullptr, false, false, false });
    return m;
}

std::string fmt(const char* f, double a)
{
    char b[96];
    std::snprintf(b, sizeof(b), f, a);
    return b;
}

// Copy [s, s+n) of every channel.
Channels slice(const AudioBuffer& a, size_t s, size_t n)
{
    Channels c(a.channels.size());
    for (size_t i = 0; i < c.size(); ++i)
        c[i].assign(a.channels[i].begin() + static_cast<long>(s), a.channels[i].begin() + static_cast<long>(s + n));
    return c;
}

// Blend the first and last `fade` frames of `p` back into `o`, so a changed
// region joins the untouched audio either side without a click. Edges at
// the very start or end of the file are left alone.
void joinEdges(Channels& p, const Channels& o, size_t fade, bool atStart, bool atEnd)
{
    const size_t n = off::frames(p);
    fade = std::min(fade, n / 2);
    for (size_t c = 0; c < p.size(); ++c)
        for (size_t i = 0; i < fade; ++i) {
            const float w = static_cast<float>(0.5 - 0.5 * std::cos(kPi * (i + 0.5) / fade));
            if (!atStart)
                p[c][i] = o[c][i] + w * (p[c][i] - o[c][i]);
            if (!atEnd)
                p[c][n - 1 - i] = o[c][n - 1 - i] + w * (p[c][n - 1 - i] - o[c][n - 1 - i]);
        }
}

std::string chName(size_t c, size_t n)
{
    if (n == 2)
        return c == 0 ? "L" : "R";
    return fmt("Ch %.0f", static_cast<double>(c + 1));
}

} // namespace

const ModuleInfo& moduleInfo(ModuleId id)
{
    static const std::vector<ModuleInfo> infos = makeInfos();
    return infos[static_cast<size_t>(id)];
}

std::vector<float> defaultModuleValues(ModuleId id)
{
    std::vector<float> v;
    for (const auto& p : moduleInfo(id).params)
        v.push_back(p.def);
    return v;
}

const std::vector<ModuleId>& moduleOrder()
{
    static const std::vector<ModuleId> order = {
        ModuleId::SpectralRepair, ModuleId::Interpolate, ModuleId::Gain,      ModuleId::Normalize,
        ModuleId::Fade,           ModuleId::Loudness,    ModuleId::EqMatch,   ModuleId::Phase,
        ModuleId::Mixing,         ModuleId::Azimuth,     ModuleId::SignalGenerator, ModuleId::TrimSilence,
        ModuleId::Resample,       ModuleId::Stats,       ModuleId::ExportFormat,
    };
    return order;
}

int eqMatchFftSize(double sampleRate)
{
    int n = 4096;
    while (n < 32768 && n < sampleRate * 0.17)
        n *= 2; // 8192 at 48 kHz
    return n;
}

bool runModule(ModuleId id, int action, const ModuleInput& in, ModuleOutput& out, std::string& error,
               const JobProgress& progress)
{
    const AudioBuffer& a = *in.audio;
    const double fs = a.sampleRate;
    const size_t total = a.numFrames();
    const size_t s = std::min(in.start, total), len = std::min(in.length, total - s);
    const ModuleInfo& info = moduleInfo(id);
    const auto& v = in.values;
    const auto step = [&](double p, const char* what) { return !progress || progress(p, what); };
    if (len == 0) {
        error = "Nothing to work on.";
        return false;
    }
    if (info.needsSelection && !in.partial) {
        error = std::string(info.name) + " needs a selection.";
        return false;
    }
    if (info.stereoOnly && a.numChannels() < 2) {
        error = std::string(info.name) + " needs a stereo file.";
        return false;
    }
    const bool atStart = s == 0, atEnd = s + len == total;
    const size_t edgeFade = static_cast<size_t>(fs * 0.005);
    // region plus up to `ctx` frames of context either side
    auto withContext = [&](size_t ctx, size_t& before) {
        before = std::min(s, ctx);
        const size_t after = std::min(total - s - len, ctx);
        return slice(a, s - before, before + len + after);
    };
    auto regionResult = [&](Channels data, const std::string& label, bool join) {
        if (join && in.partial)
            joinEdges(data, slice(a, s, len), edgeFade, atStart, atEnd);
        out.kind = ModuleOutput::Kind::Region;
        out.start = s;
        out.region = std::move(data);
        out.label = label;
    };

    switch (id) {
    case ModuleId::SpectralRepair: {
        const int mode = static_cast<int>(v[0]);
        size_t before = 0;
        const size_t ctx = static_cast<size_t>(fs * 0.6);
        Channels x = withContext(ctx, before);
        const double lo = in.hiHz > in.loHz ? in.loHz : 0.0;
        const double hi = in.hiHz > in.loHz ? in.hiHz : fs * 0.5;
        step(0.1, "Repairing");
        off::spectralRepair(x, fs, before, len, lo, hi, static_cast<off::SpectralMode>(mode), v[1] / 100.0, v[2]);
        // write back the region plus the analysis spill either side
        const size_t spill = static_cast<size_t>(2048 * std::max(1.0, std::round(fs / 48000.0)));
        const size_t a0 = before >= spill ? before - spill : 0;
        const size_t a1 = std::min(off::frames(x), before + len + spill);
        Channels r(x.size());
        for (size_t c = 0; c < x.size(); ++c)
            r[c].assign(x[c].begin() + static_cast<long>(a0), x[c].begin() + static_cast<long>(a1));
        out.kind = ModuleOutput::Kind::Region;
        out.start = s - before + a0;
        out.region = std::move(r);
        out.label = std::string("Spectral Repair (") + kSpectralModes[mode] + ")";
        return true;
    }
    case ModuleId::Interpolate: {
        if (len > static_cast<size_t>(fs * 0.1)) {
            error = "Interpolate works on selections up to 100 ms; use Spectral Repair for longer ones.";
            return false;
        }
        size_t before = 0;
        Channels x = withContext(std::max<size_t>(4096, len * 4), before);
        if (!off::interpolateGap(x, before, len)) {
            error = "Not enough audio either side of the selection to interpolate from.";
            return false;
        }
        Channels r(x.size());
        for (size_t c = 0; c < x.size(); ++c)
            r[c].assign(x[c].begin() + static_cast<long>(before), x[c].begin() + static_cast<long>(before + len));
        regionResult(std::move(r), "Interpolate", false);
        return true;
    }
    case ModuleId::Gain: {
        Channels x = slice(a, s, len);
        off::applyGain(x, v[0]);
        regionResult(std::move(x), "Gain " + fmt("%+.1f dB", v[0]), true);
        return true;
    }
    case ModuleId::Normalize: {
        Channels x = slice(a, s, len);
        if (v[1] > 0.5f)
            off::removeDc(x);
        if (off::samplePeak(x) <= 0.0) {
            error = "The selection is silent.";
            return false;
        }
        off::normalizePeak(x, v[2], v[0] > 0.5f);
        regionResult(std::move(x), "Normalize to " + fmt("%.1f dBFS", v[2]), true);
        return true;
    }
    case ModuleId::Fade: {
        Channels x = slice(a, s, len);
        const bool fadeIn = v[0] < 0.5f;
        off::applyFade(x, fadeIn, static_cast<off::FadeCurve>(static_cast<int>(v[1])));
        regionResult(std::move(x), fadeIn ? "Fade in" : "Fade out", false);
        return true;
    }
    case ModuleId::Phase: {
        size_t before = 0;
        Channels x = withContext(static_cast<size_t>(fs * 0.25), before);
        if (action == 1) {
            step(0.2, "Finding the best rotation");
            const double best = off::bestPhaseRotation(x);
            out.setValues.push_back({ 2, static_cast<float>(best) });
            out.message = "Suggested rotation: " + fmt("%.0f degrees", best);
            return true;
        }
        if (std::abs(v[2]) > 0.01f)
            off::rotatePhase(x, v[2]);
        Channels r(x.size());
        for (size_t c = 0; c < x.size(); ++c)
            r[c].assign(x[c].begin() + static_cast<long>(before), x[c].begin() + static_cast<long>(before + len));
        if (v[1] > 0.5f)
            off::removeDc(r);
        if (v[0] > 0.5f)
            off::invertPolarity(r, -1);
        std::string label = "Phase";
        if (v[0] > 0.5f)
            label += ": invert";
        if (std::abs(v[2]) > 0.01f)
            label += fmt(" rotate %.0f deg", v[2]);
        // polarity inversion can't be blended at the edges without a dip
        regionResult(std::move(r), label, v[0] < 0.5f);
        return true;
    }
    case ModuleId::Mixing: {
        Channels x = slice(a, s, len);
        const int op = static_cast<int>(v[0]);
        static const off::MixOp ops[] = { off::MixOp::SwapLR, off::MixOp::SwapLR, off::MixOp::MonoSum,
                                          off::MixOp::MidSideEncode, off::MixOp::MidSideDecode };
        if (op > 0)
            off::mixOp(x, ops[op]);
        off::channelGains(x, v[1], v[2]);
        regionResult(std::move(x), op > 0 ? std::string("Mixing: ") + kMixOps[op] : "Mixing: balance", true);
        return true;
    }
    case ModuleId::Azimuth: {
        if (action == 1) {
            step(0.2, "Measuring");
            Channels x = slice(a, s, len);
            const auto m = off::measureAzimuth(x, fs);
            out.setValues.push_back({ 0, static_cast<float>(m.delaySamples) });
            out.setValues.push_back({ 1, static_cast<float>(m.gainDb) });
            out.message = "Right channel is " + fmt("%.2f samples ", m.delaySamples) + "late and "
                + fmt("%.2f dB quieter", m.gainDb) + fmt(" (match %.0f%%)", m.correlation * 100.0);
            return true;
        }
        size_t before = 0;
        Channels x = withContext(256, before);
        off::correctAzimuth(x, v[0], v[1]);
        Channels r(x.size());
        for (size_t c = 0; c < x.size(); ++c)
            r[c].assign(x[c].begin() + static_cast<long>(before), x[c].begin() + static_cast<long>(before + len));
        regionResult(std::move(r), "Azimuth", true);
        return true;
    }
    case ModuleId::SignalGenerator: {
        Channels x = slice(a, s, len);
        const int t = static_cast<int>(v[0]);
        off::generate(x, fs, static_cast<off::SignalType>(t), v[1], v[2], static_cast<unsigned>(s + 1));
        regionResult(std::move(x), std::string("Generate: ") + kSignals[t], false);
        return true;
    }
    case ModuleId::Loudness: {
        Channels x = slice(a, s, len);
        step(0.1, "Measuring loudness");
        const auto l = off::measureLoudness(x, fs);
        out.info = { "Integrated  " + (l.integrated > -100 ? fmt("%.1f LUFS", l.integrated) : std::string("silent")),
                     "Range  " + fmt("%.1f LU", l.range) + "    Short-term max  "
                         + (l.shortTermMax > -100 ? fmt("%.1f LUFS", l.shortTermMax) : std::string("-")),
                     "True peak  " + fmt("%.1f dBTP", l.truePeakDb) + "    Sample peak  " + fmt("%.1f dBFS", l.samplePeakDb) };
        if (action == 1) {
            out.message = "Loudness " + fmt("%.1f LUFS", l.integrated) + ", true peak " + fmt("%.1f dBTP", l.truePeakDb);
            return true;
        }
        if (l.integrated < -70.0) {
            error = "Too quiet to measure: nothing to normalise.";
            return false;
        }
        off::applyGain(x, v[1] - l.integrated);
        bool limited = false;
        if (v[0] > 0.5f && off::truePeakDb(x, fs) > v[2]) {
            step(0.6, "Limiting peaks");
            off::limitPeaks(x, fs, v[2]);
            limited = true;
        }
        out.message = "Loudness set to " + fmt("%.1f LUFS", v[1]) + (limited ? " (peaks limited)" : "");
        regionResult(std::move(x), "Loudness " + fmt("%.1f LUFS", v[1]), true);
        return true;
    }
    case ModuleId::EqMatch: {
        const int n = eqMatchFftSize(fs);
        if (action == 1) {
            step(0.2, "Learning reference");
            out.eqReference = off::averageSpectrumDb(slice(a, s, len), fs, n);
            out.message = "Reference tone learned from " + fmt("%.2f s", len / fs)
                + ". Now select what should sound like it (or nothing for the whole file) and Match.";
            return true;
        }
        if (in.eqReference.size() != static_cast<size_t>(n / 2 + 1)) {
            error = "Learn a reference first: select audio with the tone you want and press Learn reference.";
            return false;
        }
        size_t before = 0;
        Channels x = withContext(static_cast<size_t>(n), before);
        step(0.2, "Matching");
        // the filter is designed from (and applied with) a little context so
        // the selection's edges are filtered cleanly
        off::eqMatch(x, fs, in.eqReference, n, v[0] / 100.0, v[1]);
        Channels r(x.size());
        for (size_t c = 0; c < x.size(); ++c)
            r[c].assign(x[c].begin() + static_cast<long>(before), x[c].begin() + static_cast<long>(before + len));
        regionResult(std::move(r), "EQ Match", true);
        return true;
    }
    case ModuleId::TrimSilence: {
        Channels x = slice(a, s, len);
        size_t removed = 0;
        Channels t = off::trimSilence(x, fs, v[0], v[1], v[2], &removed);
        if (removed == 0) {
            error = "No silences that long were found.";
            return false;
        }
        AudioBuffer w;
        w.sampleRate = fs;
        w.channels.resize(a.channels.size());
        for (size_t c = 0; c < a.channels.size(); ++c) {
            auto& d = w.channels[c];
            d.reserve(total - removed);
            d.insert(d.end(), a.channels[c].begin(), a.channels[c].begin() + static_cast<long>(s));
            d.insert(d.end(), t[c].begin(), t[c].end());
            d.insert(d.end(), a.channels[c].begin() + static_cast<long>(s + len), a.channels[c].end());
        }
        out.kind = ModuleOutput::Kind::Whole;
        out.whole = std::move(w);
        out.label = "Trim silence";
        out.message = "Removed " + fmt("%.2f s", removed / fs) + " of silence.";
        return true;
    }
    case ModuleId::Resample: {
        const double rate = kRateValues[std::clamp(static_cast<int>(v[0]), 0, 6)];
        if (std::abs(rate - fs) < 0.5) {
            error = "The file is already at that rate.";
            return false;
        }
        Channels y = off::resample(a.channels, fs, rate, [&](double p) { return step(p, "Resampling"); });
        if (y.empty()) {
            error = "Cancelled.";
            return false;
        }
        out.kind = ModuleOutput::Kind::Whole;
        out.whole.sampleRate = rate;
        out.whole.channels = std::move(y);
        out.label = std::string("Resample to ") + kRates[static_cast<int>(v[0])];
        out.message = out.label + ".";
        return true;
    }
    case ModuleId::Stats: {
        Channels x = slice(a, s, len);
        step(0.1, "Measuring levels");
        const auto st = off::channelStats(x, fs);
        out.info.push_back(fmt("%.3f s", len / fs) + fmt("  at %.1f kHz", fs / 1000.0));
        for (size_t c = 0; c < st.size(); ++c) {
            const auto& k = st[c];
            out.info.push_back(chName(c, st.size()) + ":  peak " + fmt("%.1f", k.peakDb) + "  TP " + fmt("%.1f", k.truePeakDb)
                               + "  RMS " + fmt("%.1f", k.rmsDb) + "  crest " + fmt("%.1f dB", k.crestDb));
            out.info.push_back("      DC " + fmt("%.3f%%", k.dcPercent) + "   clipped runs "
                               + fmt("%.0f", static_cast<double>(k.clippedRuns)));
        }
        step(0.5, "Measuring loudness");
        const auto l = off::measureLoudness(x, fs);
        out.info.push_back("Loudness " + (l.integrated > -100 ? fmt("%.1f LUFS", l.integrated) : std::string("-"))
                           + "   range " + fmt("%.1f LU", l.range));
        step(0.8, "Spectrum");
        out.spectrumDb = off::averageSpectrumDb(x, fs, 4096, &out.spectrumBinHz);
        out.message = "Analysed " + fmt("%.2f s", len / fs) + ".";
        return true;
    }
    default:
        error = "Nothing to do.";
        return false;
    }
}

// ---- JobRunner -----------------------------------------------------------------

JobRunner::~JobRunner()
{
    cancel();
    wait();
}

void JobRunner::wait()
{
    if (thread_.joinable())
        thread_.join();
}

std::string JobRunner::stage() const
{
    std::lock_guard<std::mutex> l(mutex_);
    return stage_;
}

void JobRunner::start(ModuleId id, int action, ModuleInput in)
{
    cancel();
    wait();
    cancel_ = false;
    done_ = false;
    progress_ = 0.0f;
    {
        std::lock_guard<std::mutex> l(mutex_);
        stage_ = moduleInfo(id).name;
    }
    running_ = true;
    thread_ = std::thread([this, id, action, in = std::move(in)] {
        ModuleOutput out;
        std::string error;
        bool ok = false;
        try {
            ok = runModule(id, action, in, out, error, [this](double p, const char* what) {
                progress_ = static_cast<float>(p);
                {
                    std::lock_guard<std::mutex> l(mutex_);
                    stage_ = what;
                }
                return !cancel_.load();
            });
        } catch (const std::bad_alloc&) {
            error = "Not enough memory for that.";
        } catch (const std::exception& e) {
            error = e.what();
        }
        {
            std::lock_guard<std::mutex> l(mutex_);
            id_ = id;
            action_ = action;
            ok_ = ok && !cancel_.load();
            out_ = std::move(out);
            error_ = cancel_.load() ? std::string("Cancelled.") : error;
        }
        done_ = true;
        running_ = false;
    });
}

bool JobRunner::take(ModuleId& id, int& action, bool& ok, ModuleOutput& out, std::string& error)
{
    if (running_.load() || !done_.load())
        return false;
    wait();
    std::lock_guard<std::mutex> l(mutex_);
    id = id_;
    action = action_;
    ok = ok_;
    out = std::move(out_);
    error = error_;
    done_ = false;
    return true;
}

} // namespace mend::editor
