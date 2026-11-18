// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
//
// The editor's one-shot "utility" modules: each has a
// parameter list drawn like the chain tools, and one or two actions
// (Apply, plus Measure/Learn/Suggest where it helps). Actions run on a
// background thread through JobRunner.
#pragma once

#include "AudioFile.hpp"
#include "Tools.hpp"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace mend::editor {

enum class ModuleId {
    SpectralRepair,
    Interpolate,
    Gain,
    Normalize,
    Fade,
    Phase,
    Mixing,
    Azimuth,
    SignalGenerator,
    Loudness,
    EqMatch,
    TrimSilence,
    Resample,
    Stats,
    ExportFormat,
    Count
};

struct ModuleInfo {
    const char* name;
    const char* blurb;
    std::vector<ToolParam> params;
    const char* primary;   // action 0 button text, or nullptr
    const char* secondary; // action 1 button text, or nullptr
    bool needsSelection;   // won't act on the whole file
    bool stereoOnly;
    bool wholeFile;        // always works on the whole file
};

const ModuleInfo& moduleInfo(ModuleId id);
std::vector<float> defaultModuleValues(ModuleId id);
const std::vector<ModuleId>& moduleOrder();

// What a module action works on. `audio` is read from the job thread: the
// editor doesn't change the document while a job runs.
struct ModuleInput {
    const AudioBuffer* audio = nullptr;
    size_t start = 0, length = 0;   // the selection, or the whole file
    bool partial = false;           // a real selection rather than the whole file
    double loHz = 0.0, hiHz = 0.0;  // spectral selection, if hiHz > loHz
    std::vector<float> values;
    std::vector<double> eqReference; // EQ Match: learned reference (dB per bin)
};

struct ModuleOutput {
    enum class Kind { None, Region, Whole } kind = Kind::None;
    size_t start = 0;
    std::vector<std::vector<float>> region; // Kind::Region
    AudioBuffer whole;                      // Kind::Whole
    std::string label;                      // undo label
    std::string message;                    // status line
    std::vector<std::string> info;          // lines to show in the module's card
    std::vector<std::pair<int, float>> setValues; // parameters the action filled in
    std::vector<double> eqReference;        // EQ Match: newly learned reference
    std::vector<double> spectrumDb;         // Stats: average spectrum for drawing
    double spectrumBinHz = 0.0;
};

using JobProgress = std::function<bool(double, const char*)>; // false = cancelled

// Runs action 0 (primary) or 1 (secondary). Returns false with `error` set
// when it can't (wrong selection, mono file for a stereo tool...).
bool runModule(ModuleId id, int action, const ModuleInput& in, ModuleOutput& out, std::string& error,
               const JobProgress& progress = {});

// FFT size used for EQ Match references.
int eqMatchFftSize(double sampleRate);

// A single background job with progress, cancel and one result.
class JobRunner {
public:
    ~JobRunner();
    void start(ModuleId id, int action, ModuleInput in);
    bool busy() const noexcept { return running_.load(); }
    float progress() const noexcept { return progress_.load(); }
    std::string stage() const;
    void cancel() noexcept { cancel_.store(true); }
    void wait();
    // Finished results (or the error). Returns false if nothing to collect.
    bool take(ModuleId& id, int& action, bool& ok, ModuleOutput& out, std::string& error);

private:
    std::thread thread_;
    std::atomic<bool> running_ { false }, cancel_ { false }, done_ { false };
    std::atomic<float> progress_ { 0.0f };
    mutable std::mutex mutex_;
    std::string stage_;
    ModuleId id_ = ModuleId::Gain;
    int action_ = 0;
    bool ok_ = false;
    ModuleOutput out_;
    std::string error_;
};

} // namespace mend::editor
