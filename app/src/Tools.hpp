// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
//
// The editor's view of the repair tools: each tool is a ToolProcessor
// wrapping one mend-core processor, described by a parameter list the UI
// draws generically. A ToolChain runs every enabled tool in a fixed,
// restoration-friendly order.
#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace mend::editor {

enum class ToolId {
    DeClip,
    DeClick,
    DeHum,
    DePlosive,
    DeNoise,
    DeCrackle,
    DeEss,
    BreathControl,
    DeWind,
    DeReverb,
    CenterExtract,
    Eq,
    Leveler,
    Voice,
    Count
};

struct ToolParam {
    const char* label;
    float min, max, def;
    enum Kind { Knob, LogKnob, IntKnob, Toggle, Choice } kind;
    const char* format;              // printf format for knobs, e.g. "%.1f dB"
    const char* const* choices = nullptr; // Choice: names for values min..max
    int offEnd = 0;                  // -1/+1: the knob's min/max end reads "Off"
};

// Settings for one tool: values for each of its parameters, plus anything
// that isn't a plain number (De-noise's learned profile).
struct ToolSettings {
    bool enabled = false;
    std::vector<float> values;
    std::string noiseProfile; // De-noise only: learned profile text, empty if none
};

const char* toolName(ToolId id);
const char* toolBlurb(ToolId id);
const std::vector<ToolParam>& toolParams(ToolId id);
ToolSettings defaultSettings(ToolId id);

// One live instance of a tool.
class ToolProcessor {
public:
    virtual ~ToolProcessor() = default;
    virtual void prepare(double sampleRate, int numChannels, int maxBlock) = 0;
    virtual void apply(const ToolSettings& s) = 0; // cheap; may be called often
    virtual int latency() const = 0;
    virtual void process(const float* const* in, float* const* out, int numChannels, int numSamples) = 0;

    // ---- live readings, so the editor's controls can move with the audio ----
    // Written on the audio thread, read on the UI thread.
    struct MeterInfo {
        const char* label;
        float max;    // full scale (dB)
        bool bipolar; // centred at zero (e.g. a gain that can go either way)
        bool cut;     // shown as a reduction ("-3.0 dB")
    };
    static constexpr int kMaxLive = 8;
    // What knob `param` shows while it is under automatic control (NaN: nothing).
    float liveValue(int param) const
    {
        return param >= 0 && param < kMaxLive ? live_[static_cast<size_t>(param)].load(std::memory_order_relaxed)
                                              : NAN;
    }
    int meterCount() const { return static_cast<int>(meterInfo_.size()); }
    const MeterInfo& meterInfo(int i) const { return meterInfo_[static_cast<size_t>(i)]; }
    float meterValue(int i) const { return meters_[static_cast<size_t>(i)].load(std::memory_order_relaxed); }

    // Used by the tool wrappers.
    void setLive(int param, float v) { live_[static_cast<size_t>(param)].store(v, std::memory_order_relaxed); }
    void addMeter(const char* label, float max, bool bipolar = false, bool cut = true)
    {
        if (meterInfo_.size() < kMaxLive)
            meterInfo_.push_back({ label, max, bipolar, cut });
    }
    void setMeter(int i, float v) { meters_[static_cast<size_t>(i)].store(v, std::memory_order_relaxed); }

protected:
    ToolProcessor()
    {
        for (auto& v : live_)
            v.store(NAN);
        for (auto& v : meters_)
            v.store(0.0f);
    }
private:
    std::array<std::atomic<float>, kMaxLive> live_;
    std::array<std::atomic<float>, kMaxLive> meters_;
    std::vector<MeterInfo> meterInfo_;
};

std::unique_ptr<ToolProcessor> makeProcessor(ToolId id);

// Fixed processing order. Clipping and clicks are repaired first (they
// would confuse the others), then hum, plosives, and broadband noise last.
const std::vector<ToolId>& chainOrder();

using ChainSettings = std::vector<ToolSettings>; // indexed by ToolId

ChainSettings defaultChain();
bool anyEnabled(const ChainSettings& c);

// De-noise: learn a profile from a stretch of audio (offline).
// Returns the profile text, or empty if the audio was too short/silent.
std::string learnNoiseProfile(const std::vector<std::vector<float>>& audio, double sampleRate);

} // namespace mend::editor
