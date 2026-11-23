// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors

#include "DistrhoPlugin.hpp"
#include "VoiceParams.hpp"

#include <algorithm>
#include <cmath>

START_NAMESPACE_DISTRHO

class VoicePlugin : public Plugin {
public:
    VoicePlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (unsigned i = 0; i < kParamCount; ++i)
            values_[i] = kVoiceParams[i].def;
        prepare();
    }

protected:
    const char* getLabel() const override { return "MendVoice"; }
    const char* getDescription() const override
    {
        return "Polishes a speaking voice: microphone correction, clarity, smoothing and de-mud in one.";
    }
    const char* getMaker() const override { return "Mend"; }
    const char* getHomePage() const override { return "https://github.com/LukeHarrison1990/mend"; }
    const char* getLicense() const override { return "MIT"; }
    uint32_t getVersion() const override { return d_version(0, 10, 0); }

    void initAudioPort(bool input, uint32_t index, AudioPort& port) override
    {
        port.groupId = kPortGroupStereo;
        Plugin::initAudioPort(input, index, port);
    }

    void initParameter(uint32_t index, Parameter& p) override
    {
        initFromSpec(kVoiceParams[index], p);
        // Named choices for the microphone and character lists.
        if (index == kMic || index == kCharacter) {
            const int n = index == kMic ? mend::VoiceClean::numMics() : mend::VoiceClean::numCharacters();
            p.hints |= kParameterIsInteger;
            p.enumValues.count = static_cast<uint8_t>(n);
            p.enumValues.restrictedMode = true;
            auto* v = new ParameterEnumerationValue[n];
            for (int i = 0; i < n; ++i) {
                v[i].value = static_cast<float>(i);
                v[i].label = index == kMic ? mend::VoiceClean::mic(i).name : mend::VoiceClean::character(i).name;
            }
            p.enumValues.values = v;
        }
    }

    float getParameterValue(uint32_t index) const override { return values_[index]; }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= kVoiceFirstOutput)
            return;
        values_[index] = value;
        pushSettings();
    }

    void activate() override { prepare(); }
    void sampleRateChanged(double) override { prepare(); }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        dsp_.process(inputs, outputs, DISTRHO_PLUGIN_NUM_INPUTS, static_cast<int>(frames));
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
        mend::VoiceClean::Settings s;
        s.mic = static_cast<int>(std::lround(values_[kMic]));
        s.character = static_cast<int>(std::lround(values_[kCharacter]));
        s.clarity = values_[kClarity];
        s.smooth = values_[kSmooth];
        s.deMud = values_[kDeMud];
        s.outputDb = values_[kOutput];
        dsp_.setSettings(s);
    }

public:
    const mend::VoiceClean::Readout& readout() const { return dsp_.readout(); }

private:
    float values_[kParamCount] = {};
    mend::VoiceClean dsp_;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VoicePlugin)
};

Plugin* createPlugin() { return new VoicePlugin(); }

END_NAMESPACE_DISTRHO

const mend::VoiceClean::Readout* mend::voiceReadout(void* pluginInstance)
{
    return pluginInstance != nullptr ? &static_cast<DISTRHO_NAMESPACE::VoicePlugin*>(pluginInstance)->readout() : nullptr;
}
