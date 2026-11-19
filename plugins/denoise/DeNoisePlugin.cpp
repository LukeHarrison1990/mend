// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors

#include "DistrhoPlugin.hpp"
#include "DeNoiseDisplay.hpp"
#include "DeNoiseParams.hpp"

#include "mend/SpectralDenoise.hpp"

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>

START_NAMESPACE_DISTRHO

class DeNoisePlugin : public Plugin {
public:
    DeNoisePlugin()
        : Plugin(kParamCount, 0, 1)
        , display_(std::make_unique<DeNoiseDisplay>())
    {
        for (unsigned i = 0; i < kParamCount; ++i)
            values_[i] = kDeNoiseParams[i].def;
        prepare();
        denoise_registry::add(static_cast<Plugin*>(this), display_.get());
    }

    ~DeNoisePlugin() override { denoise_registry::remove(static_cast<Plugin*>(this)); }

protected:
    const char* getLabel() const override { return "MendDeNoise"; }
    const char* getDescription() const override
    {
        return "Removes steady broadband noise using a learned or adaptive noise profile.";
    }
    const char* getMaker() const override { return "Mend"; }
    const char* getHomePage() const override { return "https://github.com/LukeHarrison1990/mend"; }
    const char* getLicense() const override { return "MIT"; }
    uint32_t getVersion() const override { return d_version(0, 8, 0); }

    void initAudioPort(bool input, uint32_t index, AudioPort& port) override
    {
        port.groupId = kPortGroupStereo;
        Plugin::initAudioPort(input, index, port);
    }

    void initParameter(uint32_t index, Parameter& p) override { initFromSpec(kDeNoiseParams[index], p); }
    float getParameterValue(uint32_t index) const override { return values_[index]; }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= kHasProfile)
            return;
        values_[index] = value;
        pushSettings();
    }

    // The learned noise profile is saved with the host's project.
    void initState(uint32_t, State& state) override
    {
        state.key = "profile";
        state.defaultValue = "";
        state.label = "Noise profile";
        state.hints = kStateIsOnlyForDSP;
    }

    String getState(const char* key) const override
    {
        if (std::strcmp(key, "profile") != 0)
            return String();
        return String(dsp_.profileToString().c_str());
    }

    void setState(const char* key, const char* value) override
    {
        if (std::strcmp(key, "profile") != 0)
            return;
        if (value == nullptr || value[0] == '\0')
            dsp_.clearLearnedProfile();
        else
            dsp_.profileFromString(value);
    }

    void activate() override { prepare(); }
    void sampleRateChanged(double) override { prepare(); }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        dsp_.process(inputs, outputs, DISTRHO_PLUGIN_NUM_INPUTS, static_cast<int>(frames));
        values_[kHasProfile] = dsp_.hasLearnedProfile() ? 1.0f : 0.0f;
        values_[kReductionNow] = static_cast<float>(std::clamp(dsp_.reductionAppliedDb(), 0.0, 60.0));
        display_->publish(dsp_.displayInput(), dsp_.displayOutput(), dsp_.displayNoise(), dsp_.numBins(),
                          dsp_.fftSize(), dsp_.sampleRate());
    }

private:
    void prepare()
    {
        // Keep a learned profile across re-preparation (sample rate change).
        const std::string saved = dsp_.profileToString();
        dsp_.prepare(getSampleRate(), DISTRHO_PLUGIN_NUM_INPUTS);
        if (!saved.empty())
            dsp_.profileFromString(saved);
        pushSettings();
        setLatency(static_cast<uint32_t>(dsp_.latency()));
    }

    void pushSettings()
    {
        mend::SpectralDenoise::Settings s;
        s.thresholdDb = values_[kThreshold];
        s.reductionDb = values_[kReduction];
        s.smoothing = values_[kSmoothing];
        s.learn = values_[kLearn] > 0.5f;
        s.adaptive = values_[kAdaptive] > 0.5f;
        s.noiseOnly = values_[kNoiseOnly] > 0.5f;
        dsp_.setSettings(s);
    }

    float values_[kParamCount] = {};
    mend::SpectralDenoise dsp_;
    std::unique_ptr<DeNoiseDisplay> display_;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DeNoisePlugin)
};

Plugin* createPlugin() { return new DeNoisePlugin(); }

END_NAMESPACE_DISTRHO
