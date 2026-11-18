// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#pragma once

#include "ParamSpec.hpp"

// Append new parameters at the end only: hosts store automation by index.
enum CentreExtractParam : unsigned {
    kStrength = 0,
    kAmount,
    kRemoveCentre,
    kInputLevel, // output
    kOutputLevel, // output
    kParamCount
};

static const mend::ParamSpec kCentreExtractParams[kParamCount] = {
    { "Strength", "strength", "", 0.0f, 100.0f, 50.0f, 0 },
    { "Amount", "amount", "%", 0.0f, 100.0f, 100.0f, 0 },
    { "Remove centre", "remove_centre", "", 0.0f, 1.0f, 0.0f, mend::kPBool },
    { "Input level", "input_level", "dB", -90.0f, 0.0f, -90.0f, mend::kPOutput },
    { "Output level", "output_level", "dB", -90.0f, 0.0f, -90.0f, mend::kPOutput },
};

static constexpr unsigned kCentreExtractFirstOutput = kInputLevel;
