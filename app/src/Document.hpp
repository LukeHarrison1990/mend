// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#pragma once

#include "AudioFile.hpp"

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace mend::editor {

// The open audio file, with undo/redo of rendered regions.
class Document {
public:
    bool load(const std::string& path, std::string& error);
    // dither: 0 none, 1 TPDF, 2 noise-shaped TPDF (ignored for float)
    bool exportWav(const std::string& path, WavFormat format, int dither, std::string& error) const;

    bool isOpen() const noexcept { return !audio_.empty(); }
    const AudioBuffer& audio() const noexcept { return audio_; }
    const std::string& path() const noexcept { return path_; }
    std::string displayName() const;
    bool modified() const noexcept { return modified_; }
    void markSaved() noexcept { modified_ = false; }

    // Replace audio from `start` (all channels, same length as `data`).
    void replaceRegion(size_t start, std::vector<std::vector<float>> data, const std::string& label);
    // Replace everything (length and sample rate may change), undoably.
    void replaceAll(AudioBuffer audio, const std::string& label);

    bool canUndo() const noexcept { return undoPos_ > 0; }
    bool canRedo() const noexcept { return undoPos_ < history_.size(); }
    std::string undoLabel() const { return canUndo() ? history_[undoPos_ - 1].label : std::string(); }
    std::string redoLabel() const { return canRedo() ? history_[undoPos_].label : std::string(); }
    void undo();
    void redo();

    // Bumped on every change; with the changed range, for display caches.
    uint64_t version() const noexcept { return version_; }
    size_t changedStart() const noexcept { return changedStart_; }
    size_t changedLength() const noexcept { return changedLength_; }

    // Copy a range (clamped) of all channels.
    std::vector<std::vector<float>> copyRange(size_t start, size_t length) const;

private:
    struct Edit {
        std::string label;
        size_t start;
        std::vector<std::vector<float>> before, after;
        bool whole = false; // before/after are the entire file
        double rateBefore = 0, rateAfter = 0;
        size_t bytes() const;
    };
    void writeRange(size_t start, const std::vector<std::vector<float>>& data);
    void writeWhole(const std::vector<std::vector<float>>& data, double rate);
    void pushEdit(Edit e);

    AudioBuffer audio_;
    std::string path_;
    bool modified_ = false;
    std::deque<Edit> history_;
    size_t undoPos_ = 0;
    uint64_t version_ = 0;
    size_t changedStart_ = 0, changedLength_ = 0;

    static constexpr size_t kMaxUndoBytes = size_t(1536) * 1024 * 1024; // 1.5 GB
};

// Min/max summaries for drawing waveforms quickly at any zoom.
class PeakCache {
public:
    static constexpr int kBin = 256; // frames per summary bin

    void rebuild(const AudioBuffer& audio);
    void update(const AudioBuffer& audio, size_t start, size_t length);

    // min/max of channel c over frames [from, to) — uses bins where possible
    void range(const AudioBuffer& audio, int c, size_t from, size_t to, float& mn, float& mx) const;

private:
    std::vector<std::vector<float>> min_, max_;
};

} // namespace mend::editor
