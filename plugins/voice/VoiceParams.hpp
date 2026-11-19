// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#pragma once

#include "ParamSpec.hpp"
#include "mend/VoiceClean.hpp"

// Append new parameters at the end only: hosts store automation by index.
enum VoiceParam : unsigned {
    kMic = 0,
    kCharacter,
    kClarity,
    kSmooth,
    kDeMud,
    kOutput,
    kParamCount
};

static const mend::ParamSpec kVoiceParams[kParamCount] = {
    { "Microphone", "microphone", "", 0.0f, mend::VoiceClean::kNumMics - 1.0f, 0.0f, mend::kPInt },
    { "Character", "character", "", 0.0f, mend::VoiceClean::kNumCharacters - 1.0f, 0.0f, mend::kPInt },
    { "Clarity", "clarity", "%", 0.0f, 100.0f, 50.0f, 0 },
    { "Smooth", "smooth", "%", 0.0f, 100.0f, 50.0f, 0 },
    { "De-mud", "demud", "%", 0.0f, 100.0f, 50.0f, 0 },
    { "Output", "output", "dB", -12.0f, 12.0f, 0.0f, 0 },
};

static constexpr unsigned kVoiceFirstOutput = kParamCount; // no output parameters

// What each stage is doing is read straight from the processor (the plugin
// and its UI are one object), not published as parameters.
namespace mend {
const VoiceClean::Readout* voiceReadout(void* pluginInstance);
}
