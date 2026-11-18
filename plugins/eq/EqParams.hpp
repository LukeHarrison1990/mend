// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#pragma once

#include "ParamSpec.hpp"

// Append new parameters at the end only: hosts store automation by index.
enum EqParam : unsigned {
    kHighPass = 0,
    kLowShelf,
    kLowShelfGain,
    kBand1,
    kGain1,
    kQ1,
    kBand2,
    kGain2,
    kQ2,
    kBand3,
    kGain3,
    kQ3,
    kHighShelf,
    kHighShelfGain,
    kLowPass,
    kParamCount
};

static const mend::ParamSpec kEqParams[kParamCount] = {
    { "High-pass", "high_pass", "Hz", 20.0f, 500.0f, 20.0f, mend::kPLog },
    { "Low shelf", "low_shelf", "Hz", 30.0f, 500.0f, 100.0f, mend::kPLog },
    { "Low shelf gain", "low_shelf_gain", "dB", -18.0f, 18.0f, 0.0f, 0 },
    { "Band 1", "band1", "Hz", 40.0f, 16000.0f, 300.0f, mend::kPLog },
    { "Gain 1", "gain1", "dB", -18.0f, 18.0f, 0.0f, 0 },
    { "Q 1", "q1", "", 0.2f, 10.0f, 1.0f, mend::kPLog },
    { "Band 2", "band2", "Hz", 40.0f, 16000.0f, 1000.0f, mend::kPLog },
    { "Gain 2", "gain2", "dB", -18.0f, 18.0f, 0.0f, 0 },
    { "Q 2", "q2", "", 0.2f, 10.0f, 1.0f, mend::kPLog },
    { "Band 3", "band3", "Hz", 40.0f, 16000.0f, 4000.0f, mend::kPLog },
    { "Gain 3", "gain3", "dB", -18.0f, 18.0f, 0.0f, 0 },
    { "Q 3", "q3", "", 0.2f, 10.0f, 1.0f, mend::kPLog },
    { "High shelf", "high_shelf", "Hz", 2000.0f, 16000.0f, 8000.0f, mend::kPLog },
    { "High shelf gain", "high_shelf_gain", "dB", -18.0f, 18.0f, 0.0f, 0 },
    { "Low-pass", "low_pass", "Hz", 2000.0f, 20000.0f, 20000.0f, mend::kPLog },
};

static constexpr unsigned kEqFirstOutput = kParamCount;
