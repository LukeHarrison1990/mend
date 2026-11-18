// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#pragma once

#include "ParamSpec.hpp"

// Append new parameters at the end only: hosts store automation by index.
enum DeCrackleParam : unsigned {
    kAmount = 0,
    kMaxWidth,
    kInputLevel, // output
    kOutputLevel, // output
    kParamCount
};

static const mend::ParamSpec kDeCrackleParams[kParamCount] = {
    { "Amount", "amount", "%", 0.0f, 100.0f, 60.0f, 0 },
    { "Max width", "max_width", "ms", 0.05f, 1.0f, 0.3f, mend::kPLog },
    { "Input level", "input_level", "dB", -90.0f, 0.0f, -90.0f, mend::kPOutput },
    { "Output level", "output_level", "dB", -90.0f, 0.0f, -90.0f, mend::kPOutput },
};

static constexpr unsigned kDeCrackleFirstOutput = kInputLevel;
