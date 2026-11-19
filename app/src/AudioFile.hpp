// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#pragma once

#include <cstddef>
#include <atomic>
#include <cstdio>
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
//
// Optional `progress` (0..1, or < 0 while the length isn't known) and
// `cancel` allow loading on a background thread.
bool loadAudioFile(const std::string& path, AudioBuffer& out, std::string& error,
                   std::atomic<float>* progress = nullptr, std::atomic<bool>* cancel = nullptr);
const char* supportedFormatsText();

enum class WavFormat { Int16, Int24, Float32 };

// Write a WAV file.
bool saveWavFile(const std::string& path, const AudioBuffer& audio, WavFormat format, std::string& error);

// fopen with a UTF-8 path on every platform.
FILE* openFileUtf8(const std::string& path, const char* mode);
// Move `from` over `to`, replacing it (for safe saves via a temporary file).
bool replaceFile(const std::string& from, const std::string& to);
// Delete a file (UTF-8 path).
void removeFileUtf8(const std::string& path);

} // namespace mend::editor
