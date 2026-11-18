// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#pragma once

#include "ParamSpec.hpp"

// Append new parameters at the end only: hosts store automation by index.
enum DePlosiveParam : unsigned {
    kSensitivity = 0,
    kStrength,
    kFrequency,
    kMaxReduction,
    kPlosivesOnly,
    kReduction, // output
    kParamCount
};

static const mend::ParamSpec kDePlosiveParams[kParamCount] = {
    { "Sensitivity", "sensitivity", "%", 0.0f, 100.0f, 50.0f, 0 },
    { "Strength", "strength", "%", 0.0f, 100.0f, 75.0f, 0 },
    { "Frequency", "frequency", "Hz", 50.0f, 300.0f, 150.0f, mend::kPLog },
    { "Max reduction", "max_reduction", "dB", 0.0f, 40.0f, 24.0f, 0 },
    { "Output plosives only", "plosives_only", "", 0.0f, 1.0f, 0.0f, mend::kPBool },
    { "Gain reduction", "reduction", "dB", 0.0f, 40.0f, 0.0f, mend::kPOutput },
};
