// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#pragma once

#include "ParamSpec.hpp"

// Append new parameters at the end only: hosts store automation by index.
enum DeReverbParam : unsigned {
    kReverbTime = 0,
    kAmount,
    kMaxReduction,
    kInputLevel, // output
    kOutputLevel, // output
    kParamCount
};

static const mend::ParamSpec kDeReverbParams[kParamCount] = {
    { "Reverb time", "reverb_time", "s", 0.1f, 5.0f, 0.8f, mend::kPLog },
    { "Amount", "amount", "%", 0.0f, 100.0f, 70.0f, 0 },
    { "Max reduction", "max_reduction", "dB", 0.0f, 30.0f, 15.0f, 0 },
    { "Input level", "input_level", "dB", -90.0f, 0.0f, -90.0f, mend::kPOutput },
    { "Output level", "output_level", "dB", -90.0f, 0.0f, -90.0f, mend::kPOutput },
};

static constexpr unsigned kDeReverbFirstOutput = kInputLevel;
