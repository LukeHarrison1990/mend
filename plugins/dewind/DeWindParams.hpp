// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#pragma once

#include "ParamSpec.hpp"

// Append new parameters at the end only: hosts store automation by index.
enum DeWindParam : unsigned {
    kFrequency = 0,
    kStrength,
    kMaxReduction,
    kInputLevel, // output
    kOutputLevel, // output
    kParamCount
};

static const mend::ParamSpec kDeWindParams[kParamCount] = {
    { "Frequency", "frequency", "Hz", 60.0f, 600.0f, 200.0f, mend::kPLog },
    { "Strength", "strength", "%", 0.0f, 100.0f, 70.0f, 0 },
    { "Max reduction", "max_reduction", "dB", 0.0f, 40.0f, 30.0f, 0 },
    { "Input level", "input_level", "dB", -90.0f, 0.0f, -90.0f, mend::kPOutput },
    { "Output level", "output_level", "dB", -90.0f, 0.0f, -90.0f, mend::kPOutput },
};

static constexpr unsigned kDeWindFirstOutput = kInputLevel;
