// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#pragma once

#include "ParamSpec.hpp"

// Append new parameters at the end only: hosts store automation by index.
enum DeClickParam : unsigned {
    kSensitivity = 0,
    kClickWidth,
    kClicksOnly,
    kClickRate, // output
    kParamCount
};

static const mend::ParamSpec kDeClickParams[kParamCount] = {
    { "Sensitivity", "sensitivity", "%", 0.0f, 100.0f, 50.0f, 0 },
    { "Max click width", "click_width", "ms", 0.1f, 5.0f, 1.0f, mend::kPLog },
    { "Output clicks only", "clicks_only", "", 0.0f, 1.0f, 0.0f, mend::kPBool },
    { "Clicks per second", "click_rate", "/s", 0.0f, 1000.0f, 0.0f, mend::kPOutput },
};
