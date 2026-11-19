// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors

#include "DistrhoPlugin.hpp"
#include "BreathControlParams.hpp"

#include "mend/Dynamics.hpp"
#include "mend/Biquad.hpp"

#include <algorithm>
#include <cmath>

START_NAMESPACE_DISTRHO

class BreathControlPlugin : public Plugin {
public:
    BreathControlPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (unsigned i = 0; i < kParamCount; ++i)
            values_[i] = kBreathControlParams[i].def;
        prepare();
    }

protected:
    const char* getLabel() const override { return "MendBreathControl"; }
    const char* getDescription() const override { return "Finds breaths between phrases and turns them down."; }
    const char* getMaker() const override { return "Mend"; }
    const char* getHomePage() const override { return "https://github.com/LukeHarrison1990/mend"; }
    const char* getLicense() const override { return "MIT"; }
    uint32_t getVersion() const override { return d_version(0, 8, 1); }

    void initAudioPort(bool input, uint32_t index, AudioPort& port) override
    {
        port.groupId = kPortGroupStereo;
        Plugin::initAudioPort(input, index, port);
    }

    void initParameter(uint32_t index, Parameter& p) override { initFromSpec(kBreathControlParams[index], p); }
    float getParameterValue(uint32_t index) const override { return values_[index]; }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= kBreathControlFirstOutput)
            return;
        values_[index] = value;
        pushSettings();
    }

    void activate() override { prepare(); }
    void sampleRateChanged(double) override { prepare(); }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        dsp_.process(inputs, outputs, DISTRHO_PLUGIN_NUM_INPUTS, static_cast<int>(frames));
        const float r = static_cast<float>(dsp_.currentReductionDb());
        values_[kReduction] = std::min(60.0f, r > values_[kReduction] ? r : values_[kReduction] * 0.9f);
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
        mend::BreathControl::Settings s;
        s.sensitivity = values_[kSensitivity];
        s.reductionDb = values_[kReductionAmount];
        dsp_.setSettings(s);
    }

    float values_[kParamCount] = {};
    mend::BreathControl dsp_;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BreathControlPlugin)
};

Plugin* createPlugin() { return new BreathControlPlugin(); }

END_NAMESPACE_DISTRHO
