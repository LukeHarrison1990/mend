// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#pragma once

#include "ParamSpec.hpp"

// Append new parameters at the end only: hosts store automation by index.
enum DeEssParam : unsigned {
    kThreshold = 0,
    kFrequency,
    kMaxReduction,
    kWideband,
    kReduction, // output
    kParamCount
};

static const mend::ParamSpec kDeEssParams[kParamCount] = {
    { "Threshold", "threshold", "dB", -60.0f, 0.0f, -30.0f, 0 },
    { "Frequency", "frequency", "Hz", 2000.0f, 12000.0f, 5000.0f, mend::kPLog },
    { "Max reduction", "max_reduction", "dB", 0.0f, 30.0f, 12.0f, 0 },
    { "Wideband", "wideband", "", 0.0f, 1.0f, 0.0f, mend::kPBool },
    { "Gain reduction", "reduction", "dB", 0.0f, 60.0f, 0.0f, mend::kPOutput },
};

static constexpr unsigned kDeEssFirstOutput = kReduction;
