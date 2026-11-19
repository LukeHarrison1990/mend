// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#include "Tools.hpp"

#include "mend/DeClick.hpp"
#include "mend/DeClip.hpp"
#include "mend/DeHum.hpp"
#include "mend/DePlosive.hpp"
#include "mend/Dynamics.hpp"
#include "mend/Eq.hpp"
#include "mend/SpectralDenoise.hpp"
#include "mend/SpectralTools.hpp"
#include "mend/VoiceClean.hpp"

#include <algorithm>
#include <cmath>

namespace mend::editor {

namespace {
using K = ToolParam::Kind;

// Parameter lists mirror the plugins' controls (minus read-only meters).
const std::vector<ToolParam> kDeClip = {
    { "Auto threshold", 0, 1, 1, K::Toggle, nullptr },
    { "Threshold", -24, 0, -0.1f, K::Knob, "%.2f dB" },
    { "Output gain", -24, 6, 0, K::Knob, "%+.1f dB" },
};
const std::vector<ToolParam> kDeClick = {
    { "Sensitivity", 0, 100, 50, K::Knob, "%.0f" },
    { "Click width", 0.1f, 5, 1, K::LogKnob, "%.2f ms" },
};
const std::vector<ToolParam> kDeHum = {
    { "Auto-detect hum", 0, 1, 1, K::Toggle, nullptr },
    { "Frequency", 40, 70, 50, K::Knob, "%.2f Hz" },
    { "Harmonics", 1, 16, 8, K::IntKnob, "%.0f" },
    { "Q", 5, 200, 40, K::LogKnob, "%.0f" },
    { "Reduction", 0, 60, 30, K::Knob, "%.1f dB" },
};
const std::vector<ToolParam> kDePlosive = {
    { "Sensitivity", 0, 100, 50, K::Knob, "%.0f" },
    { "Strength", 0, 100, 75, K::Knob, "%.0f%%" },
    { "Frequency", 50, 300, 150, K::LogKnob, "%.0f Hz" },
    { "Max reduction", 0, 40, 24, K::Knob, "%.0f dB" },
};
const std::vector<ToolParam> kDeNoise = {
    { "Adaptive", 0, 1, 1, K::Toggle, nullptr },
    { "Threshold", -10, 20, 0, K::Knob, "%+.1f dB" },
    { "Reduction", 0, 40, 12, K::Knob, "%.1f dB" },
    { "Smoothing", 0, 100, 50, K::Knob, "%.0f%%" },
};

const std::vector<ToolParam> kDeCrackle = {
    { "Amount", 0, 100, 60, K::Knob, "%.0f" },
    { "Max width", 0.05f, 1.0f, 0.3f, K::LogKnob, "%.2f ms" },
};
const std::vector<ToolParam> kDeEss = {
    { "Wideband", 0, 1, 0, K::Toggle, nullptr },
    { "Threshold", -60, 0, -30, K::Knob, "%.1f dB" },
    { "Frequency", 2000, 12000, 5000, K::LogKnob, "%.0f Hz" },
    { "Max reduction", 0, 30, 12, K::Knob, "%.1f dB" },
};
const std::vector<ToolParam> kBreath = {
    { "Sensitivity", 0, 100, 50, K::Knob, "%.0f" },
    { "Reduction", 0, 60, 12, K::Knob, "%.0f dB" },
};
const std::vector<ToolParam> kDeWind = {
    { "Frequency", 60, 600, 200, K::LogKnob, "%.0f Hz" },
    { "Strength", 0, 100, 70, K::Knob, "%.0f%%" },
    { "Max reduction", 0, 40, 30, K::Knob, "%.0f dB" },
};
const std::vector<ToolParam> kDeReverb = {
    { "Reverb time", 0.1f, 5.0f, 0.8f, K::LogKnob, "%.2f s" },
    { "Amount", 0, 100, 70, K::Knob, "%.0f%%" },
    { "Max reduction", 0, 30, 15, K::Knob, "%.0f dB" },
};
const std::vector<ToolParam> kCenter = {
    { "Remove centre", 0, 1, 0, K::Toggle, nullptr },
    { "Strength", 0, 100, 50, K::Knob, "%.0f" },
    { "Amount", 0, 100, 100, K::Knob, "%.0f%%" },
};
const std::vector<ToolParam> kEq = {
    { "High-pass", 20, 500, 20, K::LogKnob, "%.0f Hz", nullptr, -1 },
    { "Low shelf", 30, 500, 100, K::LogKnob, "%.0f Hz" },
    { "Shelf gain", -18, 18, 0, K::Knob, "%+.1f dB" },
    { "Band 1", 40, 16000, 300, K::LogKnob, "%.0f Hz" },
    { "Gain 1", -18, 18, 0, K::Knob, "%+.1f dB" },
    { "Q 1", 0.2f, 10, 1, K::LogKnob, "%.2f" },
    { "Band 2", 40, 16000, 1000, K::LogKnob, "%.0f Hz" },
    { "Gain 2", -18, 18, 0, K::Knob, "%+.1f dB" },
    { "Q 2", 0.2f, 10, 1, K::LogKnob, "%.2f" },
    { "Band 3", 40, 16000, 4000, K::LogKnob, "%.0f Hz" },
    { "Gain 3", -18, 18, 0, K::Knob, "%+.1f dB" },
    { "Q 3", 0.2f, 10, 1, K::LogKnob, "%.2f" },
    { "High shelf", 2000, 16000, 8000, K::LogKnob, "%.0f Hz" },
    { "Shelf gain", -18, 18, 0, K::Knob, "%+.1f dB" },
    { "Low-pass", 2000, 20000, 20000, K::LogKnob, "%.0f Hz", nullptr, 1 },
};
const std::vector<ToolParam> kLeveler = {
    { "Target", -40, -10, -20, K::Knob, "%.1f LUFS" },
    { "Max boost", 0, 24, 12, K::Knob, "%.0f dB" },
    { "Max cut", 0, 24, 12, K::Knob, "%.0f dB" },
    { "Speed", 0.2f, 5, 1, K::LogKnob, "%.1f s" },
    { "Gate", -70, -30, -50, K::Knob, "%.0f dB" },
};

// Voice: microphone and character names come from the core's tables.
const char* const* voiceNames(bool mics)
{
    static const auto table = [] {
        std::vector<const char*> m, c;
        for (int i = 0; i < VoiceClean::numMics(); ++i)
            m.push_back(VoiceClean::mic(i).name);
        for (int i = 0; i < VoiceClean::numCharacters(); ++i)
            c.push_back(VoiceClean::character(i).name);
        return std::make_pair(m, c);
    }();
    return mics ? table.first.data() : table.second.data();
}

const std::vector<ToolParam> kVoice = {
    { "Microphone", 0, VoiceClean::kNumMics - 1.0f, 0, K::Choice, nullptr, voiceNames(true) },
    { "Character", 0, VoiceClean::kNumCharacters - 1.0f, 0, K::Choice, nullptr, voiceNames(false) },
    { "Clarity", 0, 100, 50, K::Knob, "%.0f" },
    { "Smooth", 0, 100, 50, K::Knob, "%.0f" },
    { "De-mud", 0, 100, 50, K::Knob, "%.0f" },
    { "Output", -12, 12, 0, K::Knob, "%+.1f dB" },
};

bool on(float v) { return v > 0.5f; }

// Peak-hold for meters: rise at once, fall gently between updates.
float fall(float previous, float now) { return now >= previous ? now : previous + 0.15f * (now - previous); }

// Wraps a core processor whose process/latency/prepare look like the others.
// A meter reading taken from the processor after each block.
template <typename P>
using Reading = float (*)(const P&);

template <typename P, typename ApplyFn>
class SimpleTool : public ToolProcessor {
public:
    SimpleTool(ApplyFn fn, const char* meterLabel, float meterMax, Reading<P> reading, bool bipolar)
        : fn_(fn)
        , reading_(reading)
    {
        if (reading_ != nullptr)
            addMeter(meterLabel, meterMax, bipolar, !bipolar);
    }
    void prepare(double fs, int nch, int) override { p_.prepare(fs, nch); }
    void apply(const ToolSettings& s) override { fn_(p_, s); }
    int latency() const override { return p_.latency(); }
    void process(const float* const* in, float* const* out, int nch, int n) override
    {
        p_.process(in, out, nch, n);
        if (reading_ != nullptr) {
            held_ = bipolar() ? reading_(p_) : fall(held_, reading_(p_));
            setMeter(0, held_);
        }
    }

private:
    bool bipolar() const { return meterInfo(0).bipolar; }
    P p_;
    ApplyFn fn_;
    Reading<P> reading_ = nullptr;
    float held_ = 0.0f;
};

template <typename P, typename ApplyFn>
std::unique_ptr<ToolProcessor> simple(ApplyFn fn, const char* meterLabel = nullptr, float meterMax = 0.0f,
                                      Reading<P> reading = nullptr, bool bipolar = false)
{
    return std::make_unique<SimpleTool<P, ApplyFn>>(fn, meterLabel, meterMax, reading, bipolar);
}

// Voice: every stage of the chain gets a meter.
class VoiceTool : public ToolProcessor {
public:
    VoiceTool()
    {
        addMeter("Levelling", 12.0f, true, false);
        addMeter("De-mud", 12.0f);
        addMeter("Clarity", 6.0f, false, false);
        addMeter("Resonances", 8.0f);
        addMeter("Dynamics", 15.0f);
        addMeter("De-ess", 10.0f);
        addMeter("Limiter", 10.0f);
    }
    void prepare(double fs, int nch, int) override { p_.prepare(fs, nch); }
    void apply(const ToolSettings& s) override
    {
        VoiceClean::Settings d;
        d.mic = static_cast<int>(std::lround(s.values[0]));
        d.character = static_cast<int>(std::lround(s.values[1]));
        d.clarity = s.values[2];
        d.smooth = s.values[3];
        d.deMud = s.values[4];
        d.outputDb = s.values[5];
        p_.setSettings(d);
    }
    int latency() const override { return p_.latency(); }
    void process(const float* const* in, float* const* out, int nch, int n) override
    {
        p_.process(in, out, nch, n);
        const auto& r = p_.readout();
        const float now[7] = { r.levelDb.load(), r.deMudDb.load(), r.clarityDb.load(), r.resonanceDb.load(),
                               r.compressDb.load(), r.deEssDb.load(), r.limitDb.load() };
        for (int i = 0; i < 7; ++i) {
            held_[i] = (i == 0 || i == 1 || i == 3) ? now[i] : fall(held_[i], now[i]);
            setMeter(i, held_[i]);
        }
    }

private:
    VoiceClean p_;
    float held_[7] = {};
};

class EqTool : public ToolProcessor {
public:
    void prepare(double fs, int nch, int) override { p_.prepare(fs, nch); }
    void apply(const ToolSettings& s) override
    {
        const auto& v = s.values;
        ParametricEq::Settings e;
        e.highPassHz = v[0];
        e.lowShelfHz = v[1];
        e.lowShelfDb = v[2];
        for (int i = 0; i < 3; ++i) {
            e.peakHz[i] = v[3 + i * 3];
            e.peakDb[i] = v[4 + i * 3];
            e.peakQ[i] = v[5 + i * 3];
        }
        e.highShelfHz = v[12];
        e.highShelfDb = v[13];
        e.lowPassHz = v[14];
        p_.setSettings(e);
    }
    int latency() const override { return 0; }
    void process(const float* const* in, float* const* out, int nch, int n) override { p_.process(in, out, nch, n); }

private:
    ParametricEq p_;
};

class DeClipTool : public ToolProcessor {
public:
    void prepare(double fs, int nch, int) override { p_.prepare(fs, nch); }
    void apply(const ToolSettings& s) override
    {
        DeClip::Settings d;
        d.autoThreshold = on(s.values[0]);
        d.thresholdDb = s.values[1];
        d.outputGainDb = s.values[2];
        p_.setSettings(d);
    }
    int latency() const override { return p_.latency(); }
    void process(const float* const* in, float* const* out, int nch, int n) override
    {
        p_.process(in, out, nch, n);
        if (p_.settings().autoThreshold) // the threshold knob follows the detected clip level
            setLive(1, static_cast<float>(std::clamp(p_.clipLevelDb(), -24.0, 0.0)));
    }

private:
    DeClip p_;
};

class DeClickTool : public ToolProcessor {
public:
    void prepare(double fs, int nch, int) override { p_.prepare(fs, nch); }
    void apply(const ToolSettings& s) override
    {
        DeClick::Settings d;
        d.sensitivity = s.values[0];
        d.maxWidthMs = s.values[1];
        p_.setSettings(d);
    }
    int latency() const override { return p_.latency(); }
    void process(const float* const* in, float* const* out, int nch, int n) override { p_.process(in, out, nch, n); }

private:
    DeClick p_;
};

class DeHumTool : public ToolProcessor {
public:
    void prepare(double fs, int nch, int maxBlock) override { p_.prepare(fs, maxBlock, nch); }
    void apply(const ToolSettings& s) override
    {
        DeHum::Settings d;
        d.autoDetect = on(s.values[0]);
        d.frequency = s.values[1];
        d.harmonics = static_cast<int>(std::lround(s.values[2]));
        d.q = s.values[3];
        d.reductionDb = s.values[4];
        p_.setSettings(d);
    }
    int latency() const override { return 0; }
    void process(const float* const* in, float* const* out, int nch, int n) override
    {
        p_.process(in, out, nch, n);
        // The frequency knob follows the hum while auto-detect tracks it.
        if (p_.settings().autoDetect && p_.active())
            setLive(1, static_cast<float>(std::clamp(p_.currentFrequency(), 40.0, 70.0)));
    }

private:
    DeHum p_;
};

class DePlosiveTool : public ToolProcessor {
public:
    void prepare(double fs, int nch, int) override { p_.prepare(fs, nch); }
    void apply(const ToolSettings& s) override
    {
        DePlosive::Settings d;
        d.sensitivity = s.values[0];
        d.strength = s.values[1];
        d.frequency = s.values[2];
        d.maxReductionDb = s.values[3];
        p_.setSettings(d);
    }
    int latency() const override { return p_.latency(); }
    void process(const float* const* in, float* const* out, int nch, int n) override
    {
        p_.process(in, out, nch, n);
        held_ = fall(held_, static_cast<float>(p_.reductionDb()));
        setMeter(0, held_);
    }
    DePlosiveTool() { addMeter("Reduction", 24.0f); }

private:
    float held_ = 0.0f;
    DePlosive p_;
};

class DeNoiseTool : public ToolProcessor {
public:
    void prepare(double fs, int nch, int) override
    {
        p_.prepare(fs, nch);
        loadedProfile_.clear();
    }
    void apply(const ToolSettings& s) override
    {
        if (s.noiseProfile != loadedProfile_) {
            if (s.noiseProfile.empty())
                p_.clearLearnedProfile();
            else
                p_.profileFromString(s.noiseProfile);
            loadedProfile_ = s.noiseProfile;
        }
        SpectralDenoise::Settings d;
        // A learned profile is used whenever adaptive is off.
        d.adaptive = on(s.values[0]);
        d.thresholdDb = s.values[1];
        d.reductionDb = s.values[2];
        d.smoothing = s.values[3];
        p_.setSettings(d);
    }
    int latency() const override { return p_.latency(); }
    void process(const float* const* in, float* const* out, int nch, int n) override
    {
        p_.process(in, out, nch, n);
        setMeter(0, static_cast<float>(p_.reductionAppliedDb()));
    }
    DeNoiseTool() { addMeter("Noise reduced", 24.0f); }

private:
    SpectralDenoise p_;
    std::string loadedProfile_;
};
} // namespace

const char* toolName(ToolId id)
{
    switch (id) {
    case ToolId::DeClip: return "De-clip";
    case ToolId::DeClick: return "De-click";
    case ToolId::DeHum: return "De-hum";
    case ToolId::DePlosive: return "De-plosive";
    case ToolId::DeNoise: return "De-noise";
    case ToolId::DeCrackle: return "De-crackle";
    case ToolId::DeEss: return "De-ess";
    case ToolId::BreathControl: return "Breath Control";
    case ToolId::DeWind: return "De-wind";
    case ToolId::DeReverb: return "De-reverb";
    case ToolId::CenterExtract: return "Centre Extract";
    case ToolId::Eq: return "EQ";
    case ToolId::Leveler: return "Leveler";
    case ToolId::Voice: return "Voice";
    default: return "";
    }
}

const char* toolBlurb(ToolId id)
{
    switch (id) {
    case ToolId::DeClip: return "Rebuilds clipped peaks";
    case ToolId::DeClick: return "Removes clicks and crackle";
    case ToolId::DeHum: return "Removes mains hum";
    case ToolId::DePlosive: return "Tames p and b pops";
    case ToolId::DeNoise: return "Removes steady background noise";
    case ToolId::DeCrackle: return "Removes dense small clicks (vinyl, mouth)";
    case ToolId::DeEss: return "Tames harsh s and sh sounds";
    case ToolId::BreathControl: return "Turns down breaths between phrases";
    case ToolId::DeWind: return "Reduces wind rumble on microphones";
    case ToolId::DeReverb: return "Reduces room reverberation";
    case ToolId::CenterExtract: return "Keeps or removes the stereo centre";
    case ToolId::Eq: return "Six-band parametric EQ";
    case ToolId::Leveler: return "Evens out dialogue level";
    case ToolId::Voice: return "Polishes a speaking voice in one step";
    default: return "";
    }
}

const std::vector<ToolParam>& toolParams(ToolId id)
{
    switch (id) {
    case ToolId::DeClip: return kDeClip;
    case ToolId::DeClick: return kDeClick;
    case ToolId::DeHum: return kDeHum;
    case ToolId::DePlosive: return kDePlosive;
    case ToolId::DeCrackle: return kDeCrackle;
    case ToolId::DeEss: return kDeEss;
    case ToolId::BreathControl: return kBreath;
    case ToolId::DeWind: return kDeWind;
    case ToolId::DeReverb: return kDeReverb;
    case ToolId::CenterExtract: return kCenter;
    case ToolId::Eq: return kEq;
    case ToolId::Leveler: return kLeveler;
    case ToolId::Voice: return kVoice;
    default: return kDeNoise;
    }
}

ToolSettings defaultSettings(ToolId id)
{
    ToolSettings s;
    for (const auto& p : toolParams(id))
        s.values.push_back(p.def);
    return s;
}

std::unique_ptr<ToolProcessor> makeProcessor(ToolId id)
{
    switch (id) {
    case ToolId::DeClip: return std::make_unique<DeClipTool>();
    case ToolId::DeClick: return std::make_unique<DeClickTool>();
    case ToolId::DeHum: return std::make_unique<DeHumTool>();
    case ToolId::DePlosive: return std::make_unique<DePlosiveTool>();
    case ToolId::Voice: return std::make_unique<VoiceTool>();
    case ToolId::DeCrackle:
        return simple<DeClick>([](DeClick& p, const ToolSettings& s) {
            DeClick::Settings d; // crackle: many small, short clicks
            d.sensitivity = 50.0 + 0.5 * s.values[0];
            d.maxWidthMs = s.values[1];
            p.setSettings(d);
        });
    case ToolId::DeEss:
        return simple<DeEss>([](DeEss& p, const ToolSettings& s) {
            DeEss::Settings d;
            d.wideband = on(s.values[0]);
            d.thresholdDb = s.values[1];
            d.frequency = s.values[2];
            d.maxReductionDb = s.values[3];
            p.setSettings(d);
        }, "Reduction", 12.0f, [](const DeEss& p) { return static_cast<float>(p.currentReductionDb()); });
    case ToolId::BreathControl:
        return simple<BreathControl>([](BreathControl& p, const ToolSettings& s) {
            BreathControl::Settings d;
            d.sensitivity = s.values[0];
            d.reductionDb = s.values[1];
            p.setSettings(d);
        }, "Breaths", 24.0f, [](const BreathControl& p) { return static_cast<float>(p.currentReductionDb()); });
    case ToolId::DeWind:
        return simple<DeWind>([](DeWind& p, const ToolSettings& s) {
            DeWind::Settings d;
            d.frequency = s.values[0];
            d.strength = s.values[1];
            d.maxReductionDb = s.values[2];
            p.setSettings(d);
        });
    case ToolId::DeReverb:
        return simple<DeReverb>([](DeReverb& p, const ToolSettings& s) {
            DeReverb::Settings d;
            d.reverbTime = s.values[0];
            d.amount = s.values[1];
            d.maxReductionDb = s.values[2];
            p.setSettings(d);
        });
    case ToolId::CenterExtract:
        return simple<CenterExtract>([](CenterExtract& p, const ToolSettings& s) {
            CenterExtract::Settings d;
            d.removeCenter = on(s.values[0]);
            d.strength = s.values[1];
            d.amount = s.values[2];
            p.setSettings(d);
        });
    case ToolId::Eq: return std::make_unique<EqTool>();
    case ToolId::Leveler:
        return simple<Leveler>([](Leveler& p, const ToolSettings& s) {
            Leveler::Settings d;
            d.targetDb = s.values[0];
            d.maxBoostDb = s.values[1];
            d.maxCutDb = s.values[2];
            d.speedSeconds = s.values[3];
            d.gateDb = s.values[4];
            p.setSettings(d);
        }, "Gain", 12.0f, [](const Leveler& p) { return static_cast<float>(p.currentGainDb()); }, true);
    default: return std::make_unique<DeNoiseTool>();
    }
}

const std::vector<ToolId>& chainOrder()
{
    // damage first (clipping, clicks), then steady interference (hum,
    // wind), then the voice itself, then tone and level last
    static const std::vector<ToolId> order = {
        ToolId::DeClip,   ToolId::DeClick,       ToolId::DeCrackle, ToolId::DeHum, ToolId::DeWind,
        ToolId::DePlosive, ToolId::DeNoise,      ToolId::DeReverb,  ToolId::BreathControl, ToolId::DeEss,
        ToolId::CenterExtract, ToolId::Eq,       ToolId::Leveler,   ToolId::Voice,
    };
    return order;
}

ChainSettings defaultChain()
{
    ChainSettings c;
    for (int i = 0; i < static_cast<int>(ToolId::Count); ++i)
        c.push_back(defaultSettings(static_cast<ToolId>(i)));
    return c;
}

bool anyEnabled(const ChainSettings& c)
{
    return std::any_of(c.begin(), c.end(), [](const ToolSettings& s) { return s.enabled; });
}

std::string learnNoiseProfile(const std::vector<std::vector<float>>& audio, double sampleRate)
{
    if (audio.empty() || audio[0].empty())
        return {};
    const int nch = static_cast<int>(audio.size());
    SpectralDenoise d;
    d.prepare(sampleRate, nch);
    SpectralDenoise::Settings s;
    s.adaptive = false;
    s.learn = true;
    d.setSettings(s);
    const size_t n = audio[0].size();
    std::vector<std::vector<float>> outs(nch, std::vector<float>(4096));
    for (size_t pos = 0; pos < n; pos += 4096) {
        const int m = static_cast<int>(std::min<size_t>(4096, n - pos));
        std::vector<const float*> in(nch);
        std::vector<float*> out(nch);
        for (int c = 0; c < nch; ++c) {
            in[c] = audio[c].data() + pos;
            out[c] = outs[c].data();
        }
        d.process(in.data(), out.data(), nch, m);
    }
    // Finish learning: the profile is committed on the next block after
    // Learn is switched off. Push enough zeros to complete the last frames.
    s.learn = false;
    d.setSettings(s);
    std::vector<float> zeros(64, 0.0f);
    std::vector<const float*> in(nch, zeros.data());
    std::vector<float*> out(nch);
    for (int c = 0; c < nch; ++c)
        out[c] = outs[c].data();
    d.process(in.data(), out.data(), nch, 64);
    return d.hasLearnedProfile() ? d.profileToString() : std::string();
}

} // namespace mend::editor
