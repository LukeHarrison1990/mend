// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#pragma once

#include "ParamSpec.hpp"

// Append new parameters at the end only: hosts store automation by index.
enum LevelerParam : unsigned {
    kTarget = 0,
    kMaxBoost,
    kMaxCut,
    kSpeed,
    kGate,
    kGain, // output
    kParamCount
};

static const mend::ParamSpec kLevelerParams[kParamCount] = {
    { "Target", "target", "LUFS", -40.0f, -10.0f, -20.0f, 0 },
    { "Max boost", "max_boost", "dB", 0.0f, 24.0f, 12.0f, 0 },
    { "Max cut", "max_cut", "dB", 0.0f, 24.0f, 12.0f, 0 },
    { "Speed", "speed", "s", 0.2f, 5.0f, 1.0f, mend::kPLog },
    { "Gate", "gate", "dB", -70.0f, -30.0f, -50.0f, 0 },
    { "Gain", "gain", "dB", -24.0f, 24.0f, 0.0f, mend::kPOutput },
};

static constexpr unsigned kLevelerFirstOutput = kGain;
