// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
//
// Shared look for every Mend tool, so the suite feels like one product.
#pragma once

#include "Color.hpp"

namespace mend::ui {

using DGL_NAMESPACE::Color;

namespace theme {
// Surfaces
inline Color background() { return Color(17, 19, 23); }
inline Color panel()      { return Color(25, 28, 34); }
inline Color panelEdge()  { return Color(38, 42, 50); }
inline Color well()       { return Color(12, 14, 17); }
inline Color grid()       { return Color(255, 255, 255, 0.06f); }
inline Color gridStrong() { return Color(255, 255, 255, 0.12f); }

// Text
inline Color text()       { return Color(232, 235, 240); }
inline Color textDim()    { return Color(140, 147, 160); }
inline Color textFaint()  { return Color(90, 96, 108); }

// Accents
inline Color accent()     { return Color(64, 214, 196); }   // teal: active processing
inline Color accentSoft() { return Color(64, 214, 196, 0.18f); }
inline Color warm()       { return Color(255, 176, 72); }   // amber: detected hum / markers
inline Color warmSoft()   { return Color(255, 176, 72, 0.18f); }
inline Color knobBody()   { return Color(34, 38, 46); }
inline Color knobTrack()  { return Color(48, 53, 63); }

constexpr float radius = 10.0f;
} // namespace theme

} // namespace mend::ui
