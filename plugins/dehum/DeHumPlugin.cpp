// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors

#include "DistrhoPlugin.hpp"
#include "DeHumParams.hpp"

#include "mend/DeHum.hpp"

#include <algorithm>
#include <cmath>

START_NAMESPACE_DISTRHO

class DeHumPlugin : public Plugin {
public:
    DeHumPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (unsigned i = 0; i < kParamCount; ++i)
            values_[i] = kDeHumRanges[i].def;
        pushSettings();
        dehum_.prepare(getSampleRate(), static_cast<int>(getBufferSize()), DISTRHO_PLUGIN_NUM_INPUTS);
    }

protected:
    const char* getLabel() const override { return "MendDeHum"; }
    const char* getDescription() const override
    {
        return "Removes mains hum and its harmonics, with automatic fundamental tracking.";
    }
    const char* getMaker() const override { return "Mend"; }
    const char* getHomePage() const override { return "https://github.com/LukeHarrison1990/mend"; }
    const char* getLicense() const override { return "MIT"; }
    uint32_t getVersion() const override { return d_version(0, 10, 1); }

    void initAudioPort(bool input, uint32_t index, AudioPort& port) override
    {
        port.groupId = kPortGroupStereo;
        Plugin::initAudioPort(input, index, port);
    }

    void initParameter(uint32_t index, Parameter& p) override
    {
        p.ranges.min = kDeHumRanges[index].min;
        p.ranges.max = kDeHumRanges[index].max;
        p.ranges.def = kDeHumRanges[index].def;
        p.hints = kParameterIsAutomatable;

        switch (index) {
        case kParamFrequency:
            p.name = "Frequency"; p.symbol = "frequency"; p.unit = "Hz";
            break;
        case kParamAuto:
            p.name = "Auto detect"; p.symbol = "auto";
            p.hints |= kParameterIsBoolean | kParameterIsInteger;
            break;
        case kParamHarmonics:
            p.name = "Harmonics"; p.symbol = "harmonics";
            p.hints |= kParameterIsInteger;
            break;
        case kParamQ:
            p.name = "Q"; p.symbol = "q";
            p.hints |= kParameterIsLogarithmic;
            break;
        case kParamReduction:
            p.name = "Reduction"; p.symbol = "reduction"; p.unit = "dB";
            break;
        case kParamHumOnly:
            p.name = "Output hum only"; p.symbol = "hum_only";
            p.hints |= kParameterIsBoolean | kParameterIsInteger;
            break;
        case kParamDetectedFreq:
            p.name = "Detected frequency"; p.symbol = "detected"; p.unit = "Hz";
            p.hints = kParameterIsOutput;
            break;
        case kParamConfidence:
            p.name = "Detection confidence"; p.symbol = "confidence";
            p.hints = kParameterIsOutput;
            break;
        case kParamRemovedLevel:
            p.name = "Removed level"; p.symbol = "removed"; p.unit = "dB";
            p.hints = kParameterIsOutput;
            break;
        case kParamActive:
            p.name = "Filtering active"; p.symbol = "active";
            p.hints = kParameterIsOutput | kParameterIsBoolean;
            break;
        }
    }

    float getParameterValue(uint32_t index) const override { return values_[index]; }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= kParamCount || index >= kParamDetectedFreq)
            return;
        values_[index] = value;
        pushSettings();
    }

    void activate() override
    {
        dehum_.prepare(getSampleRate(), static_cast<int>(getBufferSize()), DISTRHO_PLUGIN_NUM_INPUTS);
    }

    void sampleRateChanged(double) override { activate(); }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        dehum_.process(inputs, outputs, DISTRHO_PLUGIN_NUM_INPUTS, static_cast<int>(frames));

        const auto& det = dehum_.detection();
        values_[kParamDetectedFreq] = static_cast<float>(dehum_.currentFrequency());
        values_[kParamConfidence] = static_cast<float>(det.confidence);
        values_[kParamRemovedLevel] = static_cast<float>(std::max(-90.0, dehum_.removedLevelDb()));
        values_[kParamActive] = dehum_.active() ? 1.0f : 0.0f;
    }

private:
    void pushSettings()
    {
        mend::DeHum::Settings s;
        s.frequency = values_[kParamFrequency];
        s.autoDetect = values_[kParamAuto] > 0.5f;
        s.harmonics = static_cast<int>(std::lround(values_[kParamHarmonics]));
        s.q = values_[kParamQ];
        s.reductionDb = values_[kParamReduction];
        s.humOnly = values_[kParamHumOnly] > 0.5f;
        dehum_.setSettings(s);
    }

    float values_[kParamCount] = {};
    mend::DeHum dehum_;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DeHumPlugin)
};

Plugin* createPlugin() { return new DeHumPlugin(); }

END_NAMESPACE_DISTRHO
