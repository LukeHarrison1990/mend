// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
//
// Audio device choice for the standalone apps and the editor: the saved
// settings (shared by every Mend app), the list of devices the system
// offers, and a live stereo in/out stream on the chosen devices.
//
// Devices are remembered by name, so a choice survives a restart and a
// replug. If a chosen device is missing, the stream falls back to the
// system default and says so; idle() switches back once it returns.
#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace mend::audio {

struct Settings {
    bool inputOn = false;
    std::string input;  // device name; empty = the system default
    int inputLeft = 1;  // device channels (1-based) feeding left and right;
    int inputRight = 2; // equal = one channel to both sides (mono)
    std::string output; // device name; empty = the system default
    unsigned sampleRate = 0; // 0 = the device's own rate
    unsigned bufferSize = 256;

    bool operator==(const Settings& o) const
    {
        return inputOn == o.inputOn && input == o.input && inputLeft == o.inputLeft && inputRight == o.inputRight
            && output == o.output && sampleRate == o.sampleRate && bufferSize == o.bufferSize;
    }
    bool operator!=(const Settings& o) const { return !(*this == o); }
};

// The settings file: %APPDATA%\Mend\audio.ini on Windows,
// ~/Library/Application Support/Mend/audio.ini on macOS,
// $XDG_CONFIG_HOME/mend/audio.ini (or ~/.config/mend) on Linux.
std::string settingsPath();
Settings loadSettings();
bool saveSettings(const Settings& s);

// "Input 1 + 2", "Input 1" and so on, for a device with `channels` inputs:
// pairs first, then each channel on its own. Each choice is {left, right}.
struct ChannelChoice {
    int left, right;
    std::string name;
};
std::vector<ChannelChoice> channelChoices(unsigned channels);

struct DeviceList {
    std::vector<std::string> inputs, outputs;
    std::string defaultInput, defaultOutput; // names of the system defaults
    std::string backend; // e.g. "WASAPI", "Core Audio", "PulseAudio"
};

// The devices the system offers now.
DeviceList listDevices();

// Common choices offered in the settings panels.
const std::vector<unsigned>& sampleRateChoices(); // 0 first (device's own)
const std::vector<unsigned>& bufferSizeChoices();

// A live stereo stream: two input and two output channels, planar float.
class Duplex {
public:
    // Called on the audio thread with at most maxBlock() frames. `in` is
    // silence when input is off.
    using Callback = void (*)(void* user, const float* const* in, float* const* out, unsigned frames);

    Duplex();
    ~Duplex();
    Duplex(const Duplex&) = delete;
    Duplex& operator=(const Duplex&) = delete;

    bool ready() const; // the audio system could be started at all
    DeviceList devices(); // fresh list (cheap enough for a settings panel)

    // Open the devices in `s` (falling back to the system defaults for any
    // that are missing, and to no input if input won't open). Doesn't start.
    bool open(const Settings& s, Callback cb, void* user);
    void close();
    bool start();
    void stop();
    bool isOpen() const { return open_; }

    // What's actually running.
    const Settings& wanted() const { return wanted_; }
    bool inputActive() const { return inputActive_; }
    const std::string& inputName() const { return inputName_; }
    const std::string& outputName() const { return outputName_; }
    unsigned inputChannels() const { return inputChannels_; }
    unsigned sampleRate() const { return sampleRate_; }
    unsigned maxBlock() const { return maxBlock_; }
    double latencyMs() const; // one buffer each way, roughly
    // Why the running devices differ from wanted() (missing device, etc.),
    // or empty.
    const std::string& note() const { return note_; }

    // Call from the UI thread now and then: reopens after a device was
    // unplugged, and moves back to the wanted devices when they return.
    // Returns true when it reopened (the rate or block size may differ).
    // With restart false the reopened stream is left stopped, for the
    // caller to adapt to it first and then start().
    bool idle(bool restart = true);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    Settings wanted_;
    Callback cb_ = nullptr;
    void* user_ = nullptr;
    bool open_ = false, started_ = false;
    bool inputActive_ = false;
    std::string inputName_, outputName_, note_;
    unsigned inputChannels_ = 0, sampleRate_ = 0, maxBlock_ = 0;
    bool usingFallback_ = false;
    int idleTicks_ = 0;
    unsigned lastCallbacks_ = 0;

    bool openExact(const Settings& s, bool inputOn, const std::string& in, const std::string& out, std::string& err);
    friend struct DuplexAccess;
};

} // namespace mend::audio
