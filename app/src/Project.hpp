// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
//
// Mend project files (.mend): the audio as it stands plus every setting,
// so a session can be closed and picked up later exactly as it was.
//
// Layout (all numbers little-endian):
//   "MENDPROJ"            8 bytes
//   format version        uint32 (1)
//   header length         uint64
//   header                UTF-8 text, one "key value" per line
//   audio                 float32 samples, channel after channel
//
// The header names tools and modules rather than numbering them, and
// unknown keys are ignored, so files keep working as tools are added.
// Undo history is not saved.
#pragma once

#include "AudioFile.hpp"
#include "Modules.hpp"
#include "Tools.hpp"

#include <atomic>
#include <string>
#include <vector>

namespace mend::editor {

struct ProjectState {
    std::string sourcePath;        // the audio file the project started from
    ChainSettings chain;           // repair chain (indexed by ToolId)
    std::vector<std::vector<float>> modules; // utility module values (indexed by ModuleId)
    float viewBlend = 0.5f;
    int freqScale = -1;            // < 0: keep the editor's current scale
    float floorDb = -110.0f;
    double viewStart = 0.0, framesPerPx = 0.0; // 0: fit the whole file
    size_t cursor = 0, selStart = 0, selEnd = 0;
    bool boxActive = false;
    double boxLoHz = 0.0, boxHiHz = 0.0;
    bool preview = true;
};

constexpr const char* kProjectExtension = ".mend";
bool isProjectFile(const std::string& path);

bool saveProject(const std::string& path, const AudioBuffer& audio, const ProjectState& state, std::string& error,
                 std::atomic<float>* progress = nullptr);
bool loadProject(const std::string& path, AudioBuffer& audio, ProjectState& state, std::string& error,
                 std::atomic<float>* progress = nullptr, std::atomic<bool>* cancel = nullptr);

} // namespace mend::editor
