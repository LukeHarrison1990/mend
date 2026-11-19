// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors

#include "DistrhoPlugin.hpp"
#include "DeClipParams.hpp"

#include "mend/DeClip.hpp"

#include <algorithm>

START_NAMESPACE_DISTRHO

class DeClipPlugin : public Plugin {
public:
    DeClipPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (unsigned i = 0; i < kParamCount; ++i)
            values_[i] = kDeClipParams[i].def;
        prepare();
    }

protected:
    const char* getLabel() const override { return "MendDeClip"; }
    const char* getDescription() const override { return "Rebuilds the flattened peaks of clipped audio."; }
    const char* getMaker() const override { return "Mend"; }
    const char* getHomePage() const override { return "https://github.com/LukeHarrison1990/mend"; }
    const char* getLicense() const override { return "MIT"; }
    uint32_t getVersion() const override { return d_version(0, 8, 1); }

    void initAudioPort(bool input, uint32_t index, AudioPort& port) override
    {
        port.groupId = kPortGroupStereo;
        Plugin::initAudioPort(input, index, port);
    }

    void initParameter(uint32_t index, Parameter& p) override { initFromSpec(kDeClipParams[index], p); }
    float getParameterValue(uint32_t index) const override { return values_[index]; }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= kClipLevel)
            return;
        values_[index] = value;
        pushSettings();
    }

    void activate() override { prepare(); }
    void sampleRateChanged(double) override { prepare(); }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        dsp_.process(inputs, outputs, DISTRHO_PLUGIN_NUM_INPUTS, static_cast<int>(frames));
        values_[kClipLevel] = static_cast<float>(std::clamp(dsp_.clipLevelDb(), -60.0, 0.0));
        values_[kClippedPct] = static_cast<float>(dsp_.clippedPercent());
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
        mend::DeClip::Settings s;
        s.autoThreshold = values_[kAutoThreshold] > 0.5f;
        s.thresholdDb = values_[kThreshold];
        s.outputGainDb = values_[kOutputGain];
        dsp_.setSettings(s);
    }

    float values_[kParamCount] = {};
    mend::DeClip dsp_;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DeClipPlugin)
};

Plugin* createPlugin() { return new DeClipPlugin(); }

END_NAMESPACE_DISTRHO
