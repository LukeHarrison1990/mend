// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#pragma once

#include "AudioFile.hpp"
#include "Tools.hpp"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace mend::editor {

// Runs the enabled tools over a region of audio on a background thread.
//
// Offline rendering can do better than real time:
//  * every tool's latency is compensated exactly, so the output lines up
//    sample for sample with the input;
//  * each tool warms up on audio before the region (or, at the very start
//    of a file, on a mirrored copy of its opening) so detectors and
//    adaptive estimates are settled from the first sample;
//  * De-hum in auto mode first scans the whole region for the hum, then
//    removes it with that frequency from the start;
//  * the edges of a partial selection are crossfaded into the untouched
//    audio around it.
class Renderer {
public:
    ~Renderer();

    // Copies what it needs; the document may change afterwards.
    // With hiHz > loHz the result is confined to that frequency band (a
    // spectral selection): outside it the audio is left as it was.
    void start(const AudioBuffer& audio, size_t start, size_t length, const ChainSettings& chain, double loHz = 0.0,
               double hiHz = 0.0);

    bool busy() const noexcept { return running_.load(); }
    float progress() const noexcept { return progress_.load(); }
    std::string stage() const;
    void cancel() noexcept { cancel_.store(true); }

    // When finished (not cancelled): the processed region. Returns false
    // if there is nothing to collect. Collecting clears the result.
    bool takeResult(size_t& start, std::vector<std::vector<float>>& out, std::string& label);

    // The same processing, synchronously (used by the worker and by tests).
    static std::vector<std::vector<float>> renderRegion(const AudioBuffer& audio, size_t start, size_t length,
                                                        const ChainSettings& chain, std::atomic<bool>* cancel,
                                                        std::atomic<float>* progress, std::string* stageOut,
                                                        std::mutex* stageMutex);

private:
    void join();

    std::thread thread_;
    std::atomic<bool> running_ { false }, cancel_ { false }, done_ { false };
    std::atomic<float> progress_ { 0.0f };
    mutable std::mutex mutex_;
    std::string stage_;

    size_t resultStart_ = 0;
    std::vector<std::vector<float>> result_;
    std::string resultLabel_;
};

} // namespace mend::editor
