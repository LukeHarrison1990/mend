/*
 * Mend addition to DPF (MIT, like the rest of Mend; DPF itself is ISC).
 *
 * Native audio for the Mend standalone apps through mend-audio (miniaudio):
 * the input and output devices, channels, sample rate and buffer size come
 * from the shared Mend audio settings and can be changed while running
 * (see the Mend additions to DistrhoStandaloneUtils.hpp).
 */

#ifndef MEND_AUDIO_BRIDGE_HPP_INCLUDED
#define MEND_AUDIO_BRIDGE_HPP_INCLUDED

#include "NativeBridge.hpp"
#include "../../extra/ScopedDenormalDisable.hpp"

#include "mend/audio/AudioDevices.hpp"

using DISTRHO_NAMESPACE::ScopedDenormalDisable;

struct MendAudioBridge : NativeBridge {
    mend::audio::Duplex duplex;
    bool active = false;

    MendAudioBridge() {}
    ~MendAudioBridge() override { duplex.close(); }

    bool open(const char* const) override
    {
        if (!duplex.ready())
            return false;
        if (!duplex.open(mend::audio::loadSettings(), &MendAudioBridge::process, this))
            return false;
        bufferSize = duplex.maxBlock();
        sampleRate = duplex.sampleRate();
        return true;
    }

    bool close() override
    {
        duplex.close();
        return true;
    }

    bool activate() override
    {
        active = duplex.start();
        return active;
    }

    bool deactivate() override
    {
        duplex.stop();
        active = false;
        return true;
    }

    bool isAudioInputEnabled() const override { return duplex.inputActive(); }

    bool supportsBufferSizeChanges() const override { return true; }

    bool requestAudioInput() override
    {
        mend::audio::Settings s = duplex.wanted();
        s.inputOn = true;
        return apply(s, true);
    }

    bool requestBufferSizeChange(const uint32_t newBufferSize) override
    {
        mend::audio::Settings s = duplex.wanted();
        s.bufferSize = newBufferSize;
        return apply(s, true);
    }

    // Reopen with new settings (keeping the plugin informed of rate and
    // block size changes). Returns false if they couldn't be used as asked.
    bool apply(const mend::audio::Settings& s, const bool save)
    {
        const bool wasActive = active;
        duplex.stop();
        const bool ok = duplex.open(s, &MendAudioBridge::process, this);
        if (!ok) // keep a working stream if at all possible
            duplex.open(mend::audio::Settings(), &MendAudioBridge::process, this);
        syncPlugin();
        if (wasActive)
            active = duplex.start();
        if (save && ok)
            mend::audio::saveSettings(s);
        return ok && duplex.note().empty();
    }

    // Called from the UI idle: follow unplugged / returning devices.
    bool idle()
    {
        if (!duplex.idle(false)) // reopened streams stay stopped until the plugin has adapted
            return false;
        syncPlugin();
        if (active)
            active = duplex.start();
        return true;
    }

private:
    void syncPlugin()
    {
        if (!duplex.isOpen())
            return;
        if (duplex.sampleRate() != sampleRate) {
            sampleRate = duplex.sampleRate();
            if (sampleRateCallback != nullptr)
                sampleRateCallback(sampleRate, jackSampleRateArg);
        }
        if (duplex.maxBlock() != bufferSize) {
            bufferSize = duplex.maxBlock();
            if (bufferSizeCallback != nullptr)
                bufferSizeCallback(bufferSize, jackBufferSizeArg);
        }
    }

    static void process(void* const user, const float* const* const in, float* const* const out, const unsigned frames)
    {
        MendAudioBridge* const self = static_cast<MendAudioBridge*>(user);
        if (self->jackProcessCallback == nullptr) {
            for (int c = 0; c < 2; ++c)
                std::memset(out[c], 0, sizeof(float) * frames);
            return;
        }
       #if DISTRHO_PLUGIN_NUM_INPUTS > 0
        for (uint i = 0; i < DISTRHO_PLUGIN_NUM_INPUTS; ++i)
            self->audioBuffers[i] = const_cast<float*>(in[i < 2 ? i : 1]);
       #endif
       #if DISTRHO_PLUGIN_NUM_OUTPUTS > 0
        for (uint i = 0; i < DISTRHO_PLUGIN_NUM_OUTPUTS && i < 2; ++i)
            self->audioBuffers[DISTRHO_PLUGIN_NUM_INPUTS + i] = out[i];
       #endif
        const ScopedDenormalDisable sdd;
        self->jackProcessCallback(frames, self->jackProcessArg);
       #if DISTRHO_PLUGIN_NUM_OUTPUTS == 1
        std::memcpy(out[1], out[0], sizeof(float) * frames);
       #endif
    }
};

#endif // MEND_AUDIO_BRIDGE_HPP_INCLUDED
