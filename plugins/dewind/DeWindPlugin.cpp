// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors

#include "DistrhoPlugin.hpp"
#include "DeWindParams.hpp"

#include "mend/SpectralTools.hpp"
#include "mend/Biquad.hpp"

#include <cmath>

START_NAMESPACE_DISTRHO

class DeWindPlugin : public Plugin {
public:
    DeWindPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (unsigned i = 0; i < kParamCount; ++i)
            values_[i] = kDeWindParams[i].def;
        prepare();
    }

protected:
    const char* getLabel() const override { return "MendDeWind"; }
    const char* getDescription() const override { return "Reduces wind rumble on microphones."; }
    const char* getMaker() const override { return "Mend"; }
    const char* getHomePage() const override { return "https://github.com/LukeHarrison1990/mend"; }
    const char* getLicense() const override { return "MIT"; }
    uint32_t getVersion() const override { return d_version(0, 10, 1); }

    void initAudioPort(bool input, uint32_t index, AudioPort& port) override
    {
        port.groupId = kPortGroupStereo;
        Plugin::initAudioPort(input, index, port);
    }

    void initParameter(uint32_t index, Parameter& p) override { initFromSpec(kDeWindParams[index], p); }
    float getParameterValue(uint32_t index) const override { return values_[index]; }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= kDeWindFirstOutput)
            return;
        values_[index] = value;
        pushSettings();
    }

    void activate() override { prepare(); }
    void sampleRateChanged(double) override { prepare(); }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        lastIn_ = inputs;
        dsp_.process(inputs, outputs, DISTRHO_PLUGIN_NUM_INPUTS, static_cast<int>(frames));
        values_[kInputLevel] = smoothLevel(values_[kInputLevel], blockLevel(inputs, frames));
        values_[kOutputLevel] = smoothLevel(values_[kOutputLevel], blockLevel(outputs, frames));
    }

private:
    void prepare()
    {
        dsp_.prepare(getSampleRate(), DISTRHO_PLUGIN_NUM_INPUTS);
        pushSettings();
        setLatency(static_cast<uint32_t>(dsp_.latency()));
        for (auto* b : { &lpIn_[0], &lpIn_[1], &lpOut_[0], &lpOut_[1] }) {
            b->reset();
            b->setCoeffs(mend::BiquadCoeffs::lowpass(getSampleRate(), 200.0, 0.707));
        }
    }

    void pushSettings()
    {
        mend::DeWind::Settings s;
        s.frequency = values_[kFrequency];
        s.strength = values_[kStrength];
        s.maxReductionDb = values_[kMaxReduction];
        dsp_.setSettings(s);
    }

    // RMS level of a block in dBFS (below 200 Hz: the band wind lives in)
    float blockLevel(const float* const* x, uint32_t frames)
    {
        double sum = 0.0;
        for (int c = 0; c < DISTRHO_PLUGIN_NUM_INPUTS; ++c)
            for (uint32_t i = 0; i < frames; ++i) {
                const double v = x == static_cast<const float* const*>(lastIn_) ? lpIn_[c].process(x[c][i]) : lpOut_[c].process(x[c][i]);
                sum += v * v;
            }
        const double ms = sum / (DISTRHO_PLUGIN_NUM_INPUTS * static_cast<double>(frames > 0 ? frames : 1));
        return static_cast<float>(10.0 * std::log10(ms + 1e-12));
    }
    static float smoothLevel(float prev, float now)
    {
        const float v = now > prev ? now : prev + 0.25f * (now - prev);
        return v < -90.0f ? -90.0f : (v > 0.0f ? 0.0f : v); // stay inside the parameter range
    }

    float values_[kParamCount] = {};
    mend::DeWind dsp_;
    mend::Biquad lpIn_[2], lpOut_[2];
    const void* lastIn_ = nullptr;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DeWindPlugin)
};

Plugin* createPlugin() { return new DeWindPlugin(); }

END_NAMESPACE_DISTRHO
