// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#pragma once

#include "ParamSpec.hpp"

// Append new parameters at the end only: hosts store automation by index.
enum DeClipParam : unsigned {
    kAutoThreshold = 0,
    kThreshold,
    kOutputGain,
    kClipLevel,   // output
    kClippedPct,  // output
    kParamCount
};

static const mend::ParamSpec kDeClipParams[kParamCount] = {
    { "Auto threshold", "auto", "", 0.0f, 1.0f, 1.0f, mend::kPBool },
    { "Threshold", "threshold", "dBFS", -24.0f, 0.0f, -0.1f, 0 },
    { "Output gain", "gain", "dB", -24.0f, 6.0f, 0.0f, 0 },
    { "Clip level", "clip_level", "dBFS", -60.0f, 0.0f, 0.0f, mend::kPOutput },
    { "Clipped samples", "clipped", "%", 0.0f, 100.0f, 0.0f, mend::kPOutput },
};
