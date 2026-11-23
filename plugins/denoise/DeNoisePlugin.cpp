// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors

#include "DistrhoPlugin.hpp"
#include "DeNoiseDisplay.hpp"
#include "DeNoiseParams.hpp"

#include "mend/DeNoiseEngine.hpp"
#include "mend/Fft.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>
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
        return "Removes background noise: a neural model for voices, or a noise profile for any sound.";
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
        initFromSpec(kDeNoiseParams[index], p);
        if (index == kMode) {
            p.enumValues.count = 2;
            p.enumValues.restrictedMode = true;
            auto* v = new ParameterEnumerationValue[2];
            v[0].value = 0.0f;
            v[0].label = "Any sound";
            v[1].value = 1.0f;
            v[1].label = "Voice";
            p.enumValues.values = v;
        }
    }
    float getParameterValue(uint32_t index) const override { return values_[index]; }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index == kHasProfile || index == kReductionNow)
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
        return String(dsp_.spectral().profileToString().c_str());
    }

    void setState(const char* key, const char* value) override
    {
        if (std::strcmp(key, "profile") != 0)
            return;
        if (value == nullptr || value[0] == '\0')
            dsp_.spectral().clearLearnedProfile();
        else
            dsp_.spectral().profileFromString(value);
    }

    void activate() override { prepare(); }
    void sampleRateChanged(double) override { prepare(); }
    void bufferSizeChanged(uint32_t) override { prepare(); }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        dsp_.process(inputs, outputs, DISTRHO_PLUGIN_NUM_INPUTS, static_cast<int>(frames));
        const auto& sp = dsp_.spectral();
        values_[kHasProfile] = sp.hasLearnedProfile() ? 1.0f : 0.0f;
        values_[kReductionNow] = static_cast<float>(std::clamp(dsp_.reductionNowDb(), 0.0, 60.0));
        if (values_[kMode] < 0.5f) {
            display_->publish(sp.displayInput(), sp.displayOutput(), sp.displayNoise(), sp.numBins(), sp.fftSize(),
                              sp.sampleRate());
        } else {
            watch_.process(inputs, outputs, DISTRHO_PLUGIN_NUM_INPUTS, static_cast<int>(frames));
            if (watch_.fresh())
                display_->publish(watch_.in, watch_.out, watch_.none, watch_.bins(), watch_.fftSize(), getSampleRate());
        }
    }

private:
    // Input and output spectra for the display in Voice mode (the neural
    // engine has no spectrum of its own at the display's resolution).
    struct SpectrumWatch {
        static constexpr int kN = 4096, kHop = 1024;
        mend::Fft fft;
        std::vector<double> window;
        std::vector<float> in, out, none, bufIn, bufOut;
        std::vector<std::complex<double>> spec, scratch;
        std::vector<double> frame;
        int fill = 0;
        bool fresh_ = false;
        int bins() const { return kN / 2 + 1; }
        int fftSize() const { return kN; }
        void prepare()
        {
            fft.prepare(kN);
            window.resize(kN);
            for (int i = 0; i < kN; ++i)
                window[static_cast<size_t>(i)] = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979 * i / kN);
            in.assign(static_cast<size_t>(bins()), 0.0f);
            out.assign(static_cast<size_t>(bins()), 0.0f);
            none.assign(static_cast<size_t>(bins()), 0.0f);
            bufIn.assign(kN, 0.0f);
            bufOut.assign(kN, 0.0f);
            spec.resize(static_cast<size_t>(bins()));
            scratch.resize(kN);
            frame.resize(kN);
            fill = 0;
        }
        bool fresh()
        {
            const bool f = fresh_;
            fresh_ = false;
            return f;
        }
        void analyse(const std::vector<float>& buf, std::vector<float>& dst)
        {
            for (int i = 0; i < kN; ++i)
                frame[static_cast<size_t>(i)] = buf[static_cast<size_t>(i)] * window[static_cast<size_t>(i)];
            fft.forwardReal(frame.data(), spec.data(), scratch.data());
            for (int k = 0; k < bins(); ++k)
                dst[static_cast<size_t>(k)] = static_cast<float>(0.7 * dst[static_cast<size_t>(k)]
                                                                 + 0.3 * std::norm(spec[static_cast<size_t>(k)]));
        }
        void process(const float** ins, float** outs, int nch, int n)
        {
            for (int i = 0; i < n; ++i) {
                float a = 0.0f, b = 0.0f;
                for (int c = 0; c < nch; ++c) {
                    a += ins[c][i];
                    b += outs[c][i];
                }
                bufIn[static_cast<size_t>(fill)] = a / nch;
                bufOut[static_cast<size_t>(fill)] = b / nch;
                if (++fill == kN) {
                    analyse(bufIn, in);
                    analyse(bufOut, out);
                    std::copy(bufIn.begin() + kHop, bufIn.end(), bufIn.begin());
                    std::copy(bufOut.begin() + kHop, bufOut.end(), bufOut.begin());
                    fill = kN - kHop;
                    fresh_ = true;
                }
            }
        }
    };

    void prepare()
    {
        // Keep a learned profile across re-preparation (sample rate change).
        const std::string saved = dsp_.spectral().profileToString();
        dsp_.prepare(getSampleRate(), DISTRHO_PLUGIN_NUM_INPUTS,
                     static_cast<int>(std::max<uint32_t>(getBufferSize(), 64)));
        if (!saved.empty())
            dsp_.spectral().profileFromString(saved);
        watch_.prepare();
        pushSettings();
        // The same in both modes, so switching never moves the audio.
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
        dsp_.setSpectralSettings(s);
        dsp_.setVoiceReductionDb(values_[kVoiceReduction]);
        dsp_.setNoiseOnly(values_[kNoiseOnly] > 0.5f);
        dsp_.setMode(values_[kMode] > 0.5f ? mend::DeNoiseEngine::Mode::Voice : mend::DeNoiseEngine::Mode::AnySound);
    }

    float values_[kParamCount] = {};
    mend::DeNoiseEngine dsp_;
    SpectrumWatch watch_;
    std::unique_ptr<DeNoiseDisplay> display_;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DeNoisePlugin)
};

Plugin* createPlugin() { return new DeNoisePlugin(); }

END_NAMESPACE_DISTRHO
