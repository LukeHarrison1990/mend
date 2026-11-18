// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#include "Document.hpp"

#include "mend/Offline.hpp"

#include <algorithm>

namespace mend::editor {

bool Document::load(const std::string& path, std::string& error)
{
    AudioBuffer a;
    if (!loadAudioFile(path, a, error))
        return false;
    audio_ = std::move(a);
    path_ = path;
    modified_ = false;
    history_.clear();
    undoPos_ = 0;
    ++version_;
    changedStart_ = 0;
    changedLength_ = audio_.numFrames();
    return true;
}

bool Document::exportWav(const std::string& path, WavFormat format, int dither, std::string& error) const
{
    if (dither == 0 || format == WavFormat::Float32)
        return saveWavFile(path, audio_, format, error);
    AudioBuffer copy = audio_;
    mend::offline::ditherTo(copy.channels, format == WavFormat::Int16 ? 16 : 24, dither == 2);
    return saveWavFile(path, copy, format, error);
}

std::string Document::displayName() const
{
    const size_t slash = path_.find_last_of("/\\");
    return slash == std::string::npos ? path_ : path_.substr(slash + 1);
}

size_t Document::Edit::bytes() const
{
    size_t b = 0;
    for (const auto& c : before)
        b += c.size() * sizeof(float);
    for (const auto& c : after)
        b += c.size() * sizeof(float);
    return b;
}

std::vector<std::vector<float>> Document::copyRange(size_t start, size_t length) const
{
    std::vector<std::vector<float>> out(audio_.numChannels());
    const size_t n = audio_.numFrames();
    start = std::min(start, n);
    length = std::min(length, n - start);
    for (int c = 0; c < audio_.numChannels(); ++c)
        out[c].assign(audio_.channels[c].begin() + static_cast<long>(start),
                      audio_.channels[c].begin() + static_cast<long>(start + length));
    return out;
}

void Document::writeRange(size_t start, const std::vector<std::vector<float>>& data)
{
    for (int c = 0; c < audio_.numChannels() && c < static_cast<int>(data.size()); ++c)
        std::copy(data[c].begin(), data[c].end(), audio_.channels[c].begin() + static_cast<long>(start));
    ++version_;
    changedStart_ = start;
    changedLength_ = data.empty() ? 0 : data[0].size();
    modified_ = true;
}

void Document::replaceRegion(size_t start, std::vector<std::vector<float>> data, const std::string& label)
{
    if (data.empty() || data[0].empty() || start + data[0].size() > audio_.numFrames())
        return;
    Edit e;
    e.label = label;
    e.start = start;
    e.before = copyRange(start, data[0].size());
    e.after = std::move(data);
    writeRange(start, e.after);
    pushEdit(std::move(e));
}

void Document::replaceAll(AudioBuffer audio, const std::string& label)
{
    if (audio.empty())
        return;
    Edit e;
    e.label = label;
    e.start = 0;
    e.whole = true;
    e.before = audio_.channels;
    e.rateBefore = audio_.sampleRate;
    e.after = std::move(audio.channels);
    e.rateAfter = audio.sampleRate;
    writeWhole(e.after, e.rateAfter);
    pushEdit(std::move(e));
}

void Document::writeWhole(const std::vector<std::vector<float>>& data, double rate)
{
    audio_.channels = data;
    audio_.sampleRate = rate;
    ++version_;
    changedStart_ = 0;
    changedLength_ = ~size_t(0) / 2; // everything (forces a full redraw)
    modified_ = true;
}

void Document::pushEdit(Edit e)
{
    history_.erase(history_.begin() + static_cast<long>(undoPos_), history_.end()); // drop redo
    history_.push_back(std::move(e));
    undoPos_ = history_.size();

    // keep undo memory bounded: forget the oldest steps first
    size_t total = 0;
    for (const auto& h : history_)
        total += h.bytes();
    while (history_.size() > 1 && total > kMaxUndoBytes) {
        total -= history_.front().bytes();
        history_.pop_front();
        --undoPos_;
    }
}

void Document::undo()
{
    if (!canUndo())
        return;
    const Edit& e = history_[--undoPos_];
    if (e.whole)
        writeWhole(e.before, e.rateBefore);
    else
        writeRange(e.start, e.before);
}

void Document::redo()
{
    if (!canRedo())
        return;
    const Edit& e = history_[undoPos_++];
    if (e.whole)
        writeWhole(e.after, e.rateAfter);
    else
        writeRange(e.start, e.after);
}

// ---------------------------------------------------------------------------

void PeakCache::rebuild(const AudioBuffer& audio)
{
    const size_t bins = (audio.numFrames() + kBin - 1) / kBin;
    min_.assign(audio.numChannels(), std::vector<float>(bins, 0.0f));
    max_.assign(audio.numChannels(), std::vector<float>(bins, 0.0f));
    update(audio, 0, audio.numFrames());
}

void PeakCache::update(const AudioBuffer& audio, size_t start, size_t length)
{
    if (min_.size() != static_cast<size_t>(audio.numChannels())) {
        rebuild(audio);
        return;
    }
    const size_t n = audio.numFrames();
    const size_t b0 = start / kBin, b1 = std::min((start + length + kBin - 1) / kBin, min_.empty() ? 0 : min_[0].size());
    for (int c = 0; c < audio.numChannels(); ++c) {
        const float* x = audio.channels[c].data();
        for (size_t b = b0; b < b1; ++b) {
            const size_t f0 = b * kBin, f1 = std::min(n, f0 + kBin);
            float mn = x[f0], mx = x[f0];
            for (size_t i = f0 + 1; i < f1; ++i) {
                mn = std::min(mn, x[i]);
                mx = std::max(mx, x[i]);
            }
            min_[c][b] = mn;
            max_[c][b] = mx;
        }
    }
}

void PeakCache::range(const AudioBuffer& audio, int c, size_t from, size_t to, float& mn, float& mx) const
{
    const size_t n = audio.numFrames();
    to = std::min(to, n);
    if (from >= to) {
        mn = mx = 0.0f;
        return;
    }
    const float* x = audio.channels[c].data();
    mn = mx = x[from];
    size_t i = from;
    // leading partial bin
    while (i < to && i % kBin != 0) {
        mn = std::min(mn, x[i]);
        mx = std::max(mx, x[i]);
        ++i;
    }
    // whole bins
    while (i + kBin <= to) {
        const size_t b = i / kBin;
        mn = std::min(mn, min_[c][b]);
        mx = std::max(mx, max_[c][b]);
        i += kBin;
    }
    // trailing samples
    for (; i < to; ++i) {
        mn = std::min(mn, x[i]);
        mx = std::max(mx, x[i]);
    }
}

} // namespace mend::editor
