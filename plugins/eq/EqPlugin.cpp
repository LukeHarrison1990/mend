// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors

#include "DistrhoPlugin.hpp"
#include "EqParams.hpp"

#include "mend/Eq.hpp"
#include "mend/Biquad.hpp"

#include <cmath>

START_NAMESPACE_DISTRHO

class EqPlugin : public Plugin {
public:
    EqPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (unsigned i = 0; i < kParamCount; ++i)
            values_[i] = kEqParams[i].def;
        prepare();
    }

protected:
    const char* getLabel() const override { return "MendEq"; }
    const char* getDescription() const override { return "Six-band parametric EQ with high- and low-pass filters."; }
    const char* getMaker() const override { return "Mend"; }
    const char* getHomePage() const override { return "https://github.com/LukeHarrison1990/mend"; }
    const char* getLicense() const override { return "MIT"; }
    uint32_t getVersion() const override { return d_version(0, 8, 0); }

    void initAudioPort(bool input, uint32_t index, AudioPort& port) override
    {
        port.groupId = kPortGroupStereo;
        Plugin::initAudioPort(input, index, port);
    }

    void initParameter(uint32_t index, Parameter& p) override { initFromSpec(kEqParams[index], p); }
    float getParameterValue(uint32_t index) const override { return values_[index]; }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= kEqFirstOutput)
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
    }

    void pushSettings()
    {
        mend::ParametricEq::Settings s;
        s.highPassHz = values_[kHighPass];
        s.lowShelfHz = values_[kLowShelf];
        s.lowShelfDb = values_[kLowShelfGain];
        s.peakHz = { values_[kBand1], values_[kBand2], values_[kBand3] };
        s.peakDb = { values_[kGain1], values_[kGain2], values_[kGain3] };
        s.peakQ = { values_[kQ1], values_[kQ2], values_[kQ3] };
        s.highShelfHz = values_[kHighShelf];
        s.highShelfDb = values_[kHighShelfGain];
        s.lowPassHz = values_[kLowPass];
        dsp_.setSettings(s);
    }

    float values_[kParamCount] = {};
    mend::ParametricEq dsp_;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EqPlugin)
};

Plugin* createPlugin() { return new EqPlugin(); }

END_NAMESPACE_DISTRHO
