// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#pragma once

#include "AudioFile.hpp"
#include "Document.hpp"

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

namespace mend::editor {

enum class FreqScale { Linear, Mel, Log };

// What part of the file to draw and at what size. One column per physical
// pixel; the top row is the highest frequency.
struct SpecView {
    double viewStart = 0.0;    // frames
    double framesPerCol = 1.0; // frames per physical pixel column
    int cols = 0;
    int rows = 0; // per channel lane
    FreqScale scale = FreqScale::Mel;
    float floorDb = -110.0f; // dBFS shown as black
    uint64_t docVersion = 0;

    bool operator==(const SpecView& o) const noexcept
    {
        return viewStart == o.viewStart && framesPerCol == o.framesPerCol && cols == o.cols && rows == o.rows
            && scale == o.scale && floorDb == o.floorDb && docVersion == o.docVersion;
    }
    bool operator!=(const SpecView& o) const noexcept { return !(*this == o); }
};

// Frequency axis mapping: position 0 = top (Nyquist), 1 = bottom.
double specFreqAt(FreqScale s, double fs, double pos);
double specPosOf(FreqScale s, double fs, double hz);
// The analysis length used for a sample rate (about 46 ms).
int specFftSize(double fs);

// Draw one channel as RGBA8 (rows x cols, opaque), single-threaded.
void renderSpectrogram(const AudioBuffer& a, int channel, const SpecView& v, std::vector<uint8_t>& rgba);

// Colour map used for the spectrogram (t in 0..1).
void specColour(float t, uint8_t& r, uint8_t& g, uint8_t& b);

// Draw a waveform lane as RGBA8 (w x h, straight alpha) from the peak
// cache: one filled min/max span per physical column, anti-aliased ends.
void rasterWaveform(const AudioBuffer& a, const PeakCache& peaks, int channel, double viewStart, double framesPerCol,
                    int w, int h, uint8_t r, uint8_t g, uint8_t b, std::vector<uint8_t>& rgba);

// Computes spectrogram images on background threads. Only the newest
// request matters: a new one cancels whatever is in progress. All methods
// are called from the UI thread; take() also starts queued work.
class SpectrogramWorker {
public:
    ~SpectrogramWorker();

    void request(const AudioBuffer* audio, const SpecView& v);
    // A finished image set (one RGBA buffer per channel), if there is one.
    bool take(SpecView& v, std::vector<std::vector<uint8_t>>& lanes);
    // Cancel and wait until the worker no longer touches the audio.
    // Call before changing or replacing the audio it was given.
    void quiesce();
    bool busy() const noexcept { return running_.load() || pending_; }

private:
    void pump();
    void run(const AudioBuffer* audio, SpecView v, uint64_t gen);

    bool pending_ = false;
    const AudioBuffer* audio_ = nullptr;
    SpecView next_;
    std::atomic<uint64_t> generation_ { 0 };
    std::atomic<bool> running_ { false };

    std::atomic<bool> ready_ { false };
    SpecView resultView_;
    std::vector<std::vector<uint8_t>> result_;
};

} // namespace mend::editor
