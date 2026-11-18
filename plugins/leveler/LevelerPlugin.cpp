// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors

#include "DistrhoPlugin.hpp"
#include "LevelerParams.hpp"

#include "mend/Dynamics.hpp"
#include "mend/Biquad.hpp"

#include <algorithm>
#include <cmath>

START_NAMESPACE_DISTRHO

class LevelerPlugin : public Plugin {
public:
    LevelerPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (unsigned i = 0; i < kParamCount; ++i)
            values_[i] = kLevelerParams[i].def;
        prepare();
    }

protected:
    const char* getLabel() const override { return "MendLeveler"; }
    const char* getDescription() const override { return "Evens out dialogue level towards a loudness target."; }
    const char* getMaker() const override { return "Mend"; }
    const char* getHomePage() const override { return "https://github.com/LukeHarrison1990/mend"; }
    const char* getLicense() const override { return "MIT"; }
    uint32_t getVersion() const override { return d_version(0, 7, 0); }

    void initAudioPort(bool input, uint32_t index, AudioPort& port) override
    {
        port.groupId = kPortGroupStereo;
        Plugin::initAudioPort(input, index, port);
    }

    void initParameter(uint32_t index, Parameter& p) override { initFromSpec(kLevelerParams[index], p); }
    float getParameterValue(uint32_t index) const override { return values_[index]; }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= kLevelerFirstOutput)
            return;
        values_[index] = value;
        pushSettings();
    }

    void activate() override { prepare(); }
    void sampleRateChanged(double) override { prepare(); }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        dsp_.process(inputs, outputs, DISTRHO_PLUGIN_NUM_INPUTS, static_cast<int>(frames));
        values_[kGain] = std::clamp(static_cast<float>(dsp_.currentGainDb()), -24.0f, 24.0f);
    }

private:
    void prepare()
    {
        dsp_.prepare(getSampleRate(), DISTRHO_PLUGIN_NUM_INPUTS);
        pushSettings();
        setLatency(static_cast<uint32_t>(dsp_.latency()));
    }

    void pushSettings()
    {
        mend::Leveler::Settings s;
        s.targetDb = values_[kTarget];
        s.maxBoostDb = values_[kMaxBoost];
        s.maxCutDb = values_[kMaxCut];
        s.speedSeconds = values_[kSpeed];
        s.gateDb = values_[kGate];
        dsp_.setSettings(s);
    }

    float values_[kParamCount] = {};
    mend::Leveler dsp_;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LevelerPlugin)
};

Plugin* createPlugin() { return new LevelerPlugin(); }

END_NAMESPACE_DISTRHO
