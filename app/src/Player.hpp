// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#pragma once

#include "AudioFile.hpp"
#include "Tools.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

struct ma_device;

namespace mend::editor {

// Plays the document through the default output device, optionally
// previewing the enabled tools in real time.
class Player {
public:
    Player();
    ~Player();

    // The audio must stay alive and unchanged while playing; stop() first
    // before modifying it.
    bool play(const AudioBuffer* audio, size_t from, size_t to, bool preview, const ChainSettings& chain,
              std::string& error);
    void stop();
    bool playing() const noexcept { return playing_.load(); }

    // Position of the sound currently being heard (preview latency removed).
    size_t position() const noexcept;
    bool previewing() const noexcept { return preview_; }

    // Push new tool settings to the preview (same set of enabled tools).
    void updateSettings(const ChainSettings& chain);

    // The live processor for a tool while previewing (for its readings), or null.
    const ToolProcessor* processor(ToolId id) const;

    // Peak output level since the last call, per channel (for meters).
    void takePeaks(float& left, float& right);

private:
    static void callback(ma_device* device, void* out, const void* in, unsigned frames);
    void render(float* out, unsigned frames);

    std::unique_ptr<ma_device> device_;
    bool deviceOpen_ = false;
    double deviceRate_ = 0.0;

    const AudioBuffer* audio_ = nullptr;
    std::atomic<size_t> readPos_ { 0 };
    size_t endPos_ = 0;
    std::atomic<bool> playing_ { false };

    bool preview_ = false;
    std::vector<ToolId> active_;
    std::vector<std::unique_ptr<ToolProcessor>> procs_;
    int latency_ = 0;
    size_t tailLeft_ = 0;

    std::mutex settingsMutex_;
    ChainSettings pending_;
    bool pendingDirty_ = false;

    std::vector<std::vector<float>> bufA_, bufB_;
    std::atomic<float> peakL_ { 0.0f }, peakR_ { 0.0f };
};

} // namespace mend::editor
