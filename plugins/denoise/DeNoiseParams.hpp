// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#pragma once

#include "ParamSpec.hpp"

// Append new parameters at the end only: hosts store automation by index.
enum DeNoiseParam : unsigned {
    kThreshold = 0,
    kReduction,
    kSmoothing,
    kLearn,
    kAdaptive,
    kNoiseOnly,
    kHasProfile,    // output
    kReductionNow,  // output
    kParamCount
};

static const mend::ParamSpec kDeNoiseParams[kParamCount] = {
    { "Threshold", "threshold", "dB", -10.0f, 20.0f, 0.0f, 0 },
    { "Reduction", "reduction", "dB", 0.0f, 40.0f, 12.0f, 0 },
    { "Smoothing", "smoothing", "%", 0.0f, 100.0f, 50.0f, 0 },
    { "Learn noise profile", "learn", "", 0.0f, 1.0f, 0.0f, mend::kPBool },
    { "Adaptive", "adaptive", "", 0.0f, 1.0f, 1.0f, mend::kPBool },
    { "Output noise only", "noise_only", "", 0.0f, 1.0f, 0.0f, mend::kPBool },
    { "Profile learned", "has_profile", "", 0.0f, 1.0f, 0.0f, mend::kPOutput },
    { "Noise reduced", "reduced", "dB", 0.0f, 60.0f, 0.0f, mend::kPOutput },
};
