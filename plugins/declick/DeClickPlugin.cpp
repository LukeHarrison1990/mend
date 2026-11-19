// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors

#include "DistrhoPlugin.hpp"
#include "DeClickParams.hpp"

#include "mend/DeClick.hpp"

START_NAMESPACE_DISTRHO

class DeClickPlugin : public Plugin {
public:
    DeClickPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (unsigned i = 0; i < kParamCount; ++i)
            values_[i] = kDeClickParams[i].def;
        prepare();
    }

protected:
    const char* getLabel() const override { return "MendDeClick"; }
    const char* getDescription() const override { return "Removes clicks, pops and crackle while protecting transients."; }
    const char* getMaker() const override { return "Mend"; }
    const char* getHomePage() const override { return "https://github.com/LukeHarrison1990/mend"; }
    const char* getLicense() const override { return "MIT"; }
    uint32_t getVersion() const override { return d_version(0, 9, 0); }

    void initAudioPort(bool input, uint32_t index, AudioPort& port) override
    {
        port.groupId = kPortGroupStereo;
        Plugin::initAudioPort(input, index, port);
    }

    void initParameter(uint32_t index, Parameter& p) override { initFromSpec(kDeClickParams[index], p); }
    float getParameterValue(uint32_t index) const override { return values_[index]; }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= kClickRate)
            return;
        values_[index] = value;
        pushSettings();
    }

    void activate() override { prepare(); }
    void sampleRateChanged(double) override { prepare(); }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        dsp_.process(inputs, outputs, DISTRHO_PLUGIN_NUM_INPUTS, static_cast<int>(frames));
        values_[kClickRate] = static_cast<float>(dsp_.clicksPerSecond());
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
        mend::DeClick::Settings s;
        s.sensitivity = values_[kSensitivity];
        s.maxWidthMs = values_[kClickWidth];
        s.clicksOnly = values_[kClicksOnly] > 0.5f;
        dsp_.setSettings(s);
    }

    float values_[kParamCount] = {};
    mend::DeClick dsp_;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DeClickPlugin)
};

Plugin* createPlugin() { return new DeClickPlugin(); }

END_NAMESPACE_DISTRHO
