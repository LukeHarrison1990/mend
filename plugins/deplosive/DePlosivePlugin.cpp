// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors

#include "DistrhoPlugin.hpp"
#include "DePlosiveParams.hpp"

#include "mend/DePlosive.hpp"

START_NAMESPACE_DISTRHO

class DePlosivePlugin : public Plugin {
public:
    DePlosivePlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (unsigned i = 0; i < kParamCount; ++i)
            values_[i] = kDePlosiveParams[i].def;
        prepare();
    }

protected:
    const char* getLabel() const override { return "MendDePlosive"; }
    const char* getDescription() const override { return "Tames p and b pops in speech without touching the voice."; }
    const char* getMaker() const override { return "Mend"; }
    const char* getHomePage() const override { return "https://github.com/LukeHarrison1990/mend"; }
    const char* getLicense() const override { return "MIT"; }
    uint32_t getVersion() const override { return d_version(0, 7, 0); }

    void initAudioPort(bool input, uint32_t index, AudioPort& port) override
    {
        port.groupId = kPortGroupStereo;
        Plugin::initAudioPort(input, index, port);
    }

    void initParameter(uint32_t index, Parameter& p) override { initFromSpec(kDePlosiveParams[index], p); }
    float getParameterValue(uint32_t index) const override { return values_[index]; }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= kReduction)
            return;
        values_[index] = value;
        pushSettings();
    }

    void activate() override { prepare(); }
    void sampleRateChanged(double) override { prepare(); }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        dsp_.process(inputs, outputs, DISTRHO_PLUGIN_NUM_INPUTS, static_cast<int>(frames));
        // hold the deepest reduction until the UI has read it, so short
        // pops are not missed between UI refreshes
        const float r = static_cast<float>(dsp_.reductionDb());
        values_[kReduction] = r > values_[kReduction] ? r : values_[kReduction] * 0.9f;
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
        mend::DePlosive::Settings s;
        s.sensitivity = values_[kSensitivity];
        s.strength = values_[kStrength];
        s.frequency = values_[kFrequency];
        s.maxReductionDb = values_[kMaxReduction];
        s.plosivesOnly = values_[kPlosivesOnly] > 0.5f;
        dsp_.setSettings(s);
    }

    float values_[kParamCount] = {};
    mend::DePlosive dsp_;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DePlosivePlugin)
};

Plugin* createPlugin() { return new DePlosivePlugin(); }

END_NAMESPACE_DISTRHO
