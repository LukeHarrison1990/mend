// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace mend::editor {

// Planar float audio in memory.
struct AudioBuffer {
    double sampleRate = 48000.0;
    std::vector<std::vector<float>> channels;

    int numChannels() const noexcept { return static_cast<int>(channels.size()); }
    size_t numFrames() const noexcept { return channels.empty() ? 0 : channels[0].size(); }
    double seconds() const noexcept { return sampleRate > 0 ? numFrames() / sampleRate : 0.0; }
    bool empty() const noexcept { return numFrames() == 0; }
};

// Decode an audio file at its own rate and channel count. Paths are UTF-8.
// WAV, AIFF, FLAC, MP3 and Ogg Vorbis via miniaudio; Opus via opusfile;
// AAC, ALAC, WMA etc. via the OS (Media Foundation on Windows, Audio
// Toolbox on macOS, FFmpeg if installed elsewhere). Returns false with a
// readable message on failure.
bool loadAudioFile(const std::string& path, AudioBuffer& out, std::string& error);
const char* supportedFormatsText();

enum class WavFormat { Int16, Int24, Float32 };

// Write a WAV file.
bool saveWavFile(const std::string& path, const AudioBuffer& audio, WavFormat format, std::string& error);

} // namespace mend::editor
