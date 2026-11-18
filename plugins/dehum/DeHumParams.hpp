// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#pragma once

// Parameter indices shared by the DSP and the UI. Append new parameters at
// the end only: hosts store automation and presets by index.
enum DeHumParam : unsigned {
    kParamFrequency = 0,
    kParamAuto,
    kParamHarmonics,
    kParamQ,
    kParamReduction,
    kParamHumOnly,
    // outputs (DSP -> UI meters)
    kParamDetectedFreq,
    kParamConfidence,
    kParamRemovedLevel,
    kParamActive,       // output: 1 while filters are applied
    kParamCount
};

struct DeHumParamRange { float min, max, def; };

static constexpr DeHumParamRange kDeHumRanges[kParamCount] = {
    { 40.0f, 70.0f, 50.0f },    // Frequency (Hz)
    { 0.0f, 1.0f, 1.0f },       // Auto detect
    { 1.0f, 16.0f, 8.0f },      // Harmonics
    { 5.0f, 200.0f, 40.0f },    // Q
    { 0.0f, 60.0f, 30.0f },     // Reduction (dB)
    { 0.0f, 1.0f, 0.0f },       // Hum only
    { 40.0f, 70.0f, 50.0f },    // Detected frequency (Hz)
    { 0.0f, 1.0f, 0.0f },       // Confidence
    { -90.0f, 0.0f, -90.0f },   // Removed level (dBFS)
    { 0.0f, 1.0f, 0.0f },       // Filtering active
};
