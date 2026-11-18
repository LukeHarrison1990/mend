// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#pragma once

#include "ParamSpec.hpp"

// Append new parameters at the end only: hosts store automation by index.
enum BreathControlParam : unsigned {
    kSensitivity = 0,
    kReductionAmount,
    kReduction, // output
    kParamCount
};

static const mend::ParamSpec kBreathControlParams[kParamCount] = {
    { "Sensitivity", "sensitivity", "", 0.0f, 100.0f, 50.0f, 0 },
    { "Reduction", "reduction_amount", "dB", 0.0f, 60.0f, 12.0f, 0 },
    { "Gain reduction", "reduction", "dB", 0.0f, 60.0f, 0.0f, mend::kPOutput },
};

static constexpr unsigned kBreathControlFirstOutput = kReduction;
