// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
//
// Lightweight, NanoVG-drawn controls shared by all Mend plugin UIs.
// Controls are plain structs: the owning UI lays them out, forwards mouse
// events and talks to the host. This keeps each tool's UI file short and
// every tool consistent.
#pragma once

#include "NanoVG.hpp"
#include "mend/ui/Theme.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace mend::ui {

using DGL_NAMESPACE::NanoVG;

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    bool contains(double px, double py) const { return px >= x && px < x + w && py >= y && py < y + h; }
    float cx() const { return x + w * 0.5f; }
    float cy() const { return y + h * 0.5f; }
};

inline void drawPanel(NanoVG& vg, const Rect& r, float radius = theme::radius)
{
    vg.beginPath();
    vg.roundedRect(r.x, r.y, r.w, r.h, radius);
    vg.fillColor(theme::panel());
    vg.fill();
    vg.strokeColor(theme::panelEdge());
    vg.strokeWidth(1.0f);
    vg.stroke();
}

inline void drawText(NanoVG& vg, float x, float y, float size, const Color& c, int align, const char* s)
{
    vg.fontSize(size);
    vg.fillColor(c);
    vg.textAlign(align);
    vg.text(x, y, s, nullptr);
}

// ---------------------------------------------------------------------------
// Rotary knob: vertical drag, Shift for fine control, scroll wheel,
// double-click to reset to default.

struct Knob {
    Rect bounds;
    unsigned param = 0;
    std::string label;
    float min = 0, max = 1, def = 0;
    bool logarithmic = false;
    bool integer = false;
    bool dimmed = false;  // e.g. a control that has no effect in the current mode
    // Under automatic control: the knob follows a detected or adaptive
    // value (drawn in amber with an AUTO tag); grabbing it takes over.
    bool automatic = false;
    std::function<bool()> isAutomatic;
    std::function<std::string(float)> format;
    // Optional hooks: a live value to show instead of the parameter, and
    // an action when the knob is grabbed (e.g. leave auto mode).
    std::function<bool()> isDimmed;
    std::function<float()> displayValue;
    std::function<void()> onGrab;

    float value = 0;
    float eased = 0;        // what an automatic knob currently shows (glides)
    bool easedValid = false;

    float toNorm(float v) const
    {
        v = std::clamp(v, min, max);
        if (logarithmic)
            return std::log(v / min) / std::log(max / min);
        return (v - min) / (max - min);
    }
    float fromNorm(float n) const
    {
        n = std::clamp(n, 0.0f, 1.0f);
        float v = logarithmic ? min * std::pow(max / min, n) : min + n * (max - min);
        if (integer)
            v = std::round(v);
        return v;
    }

    void draw(NanoVG& vg, float displayValue) const
    {
        const float cx = bounds.cx();
        const float radius = std::min(bounds.w, bounds.h - 44.0f) * 0.5f;
        const float cy = bounds.y + 22.0f + radius;
        const float start = 0.75f * 3.14159265f;   // 135 deg
        const float sweep = 1.5f * 3.14159265f;    // 270 deg
        const float n = toNorm(displayValue);
        const Color active = dimmed ? theme::textFaint() : automatic ? theme::warm() : theme::accent();

        drawText(vg, cx, bounds.y + 8.0f, 11.0f, theme::textDim(),
                 NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, label.c_str());

        // track
        vg.beginPath();
        vg.arc(cx, cy, radius, start, start + sweep, NanoVG::CW);
        vg.strokeColor(theme::knobTrack());
        vg.strokeWidth(4.0f);
        vg.lineCap(NanoVG::ROUND);
        vg.stroke();

        // value arc
        if (n > 0.001f) {
            vg.beginPath();
            vg.arc(cx, cy, radius, start, start + sweep * n, NanoVG::CW);
            vg.strokeColor(active);
            vg.strokeWidth(4.0f);
            vg.stroke();
        }

        // body with soft shading
        const float br = radius - 8.0f;
        vg.beginPath();
        vg.circle(cx, cy, br);
        vg.fillPaint(vg.radialGradient(cx, cy - br * 0.4f, br * 0.1f, br * 1.2f,
                                       Color(48, 53, 63), theme::knobBody()));
        vg.fill();
        vg.strokeColor(Color(255, 255, 255, 0.05f));
        vg.strokeWidth(1.0f);
        vg.stroke();

        // pointer
        const float a = start + sweep * n;
        vg.beginPath();
        vg.moveTo(cx + std::cos(a) * br * 0.35f, cy + std::sin(a) * br * 0.35f);
        vg.lineTo(cx + std::cos(a) * br * 0.85f, cy + std::sin(a) * br * 0.85f);
        vg.strokeColor(dimmed ? theme::textFaint() : theme::text());
        vg.strokeWidth(2.5f);
        vg.stroke();

        const std::string txt = format ? format(displayValue) : std::to_string(displayValue);
        drawText(vg, cx, cy + radius + 14.0f, 13.0f, dimmed ? theme::textDim() : automatic ? theme::warm() : theme::text(),
                 NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, txt.c_str());
        if (automatic) {
            const float tw = 34.0f, tx = cx - tw * 0.5f, ty = cy - 7.0f;
            vg.beginPath();
            vg.roundedRect(tx, ty, tw, 14.0f, 7.0f);
            vg.fillColor(theme::warmSoft());
            vg.fill();
            drawText(vg, cx, ty + 7.5f, 9.0f, theme::warm(), NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, "AUTO");
        }
    }
};

// ---------------------------------------------------------------------------
// Pill-shaped on/off switch with a label.

struct Toggle {
    Rect bounds;
    unsigned param = 0;
    std::string label;
    bool on = false;
    std::function<void(bool)> onChange; // optional, after the change is sent

    void draw(NanoVG& vg) const
    {
        const float h = bounds.h;
        vg.beginPath();
        vg.roundedRect(bounds.x, bounds.y, bounds.w, h, h * 0.5f);
        vg.fillColor(on ? theme::accentSoft() : theme::well());
        vg.fill();
        vg.strokeColor(on ? theme::accent() : theme::panelEdge());
        vg.strokeWidth(1.0f);
        vg.stroke();

        // switch
        const float sw = 30.0f, sh = 16.0f;
        const float sx = bounds.x + 12.0f, sy = bounds.cy() - sh * 0.5f;
        vg.beginPath();
        vg.roundedRect(sx, sy, sw, sh, sh * 0.5f);
        vg.fillColor(on ? theme::accent() : theme::knobTrack());
        vg.fill();
        vg.beginPath();
        vg.circle(on ? sx + sw - sh * 0.5f : sx + sh * 0.5f, sy + sh * 0.5f, sh * 0.5f - 2.5f);
        vg.fillColor(on ? theme::well() : theme::textDim());
        vg.fill();

        drawText(vg, sx + sw + 10.0f, bounds.cy(), 12.5f, on ? theme::text() : theme::textDim(),
                 NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, label.c_str());
    }
};

// ---------------------------------------------------------------------------
// Horizontal level meter in dBFS.

struct Meter {
    Rect bounds;
    std::string label;
    float minDb = -90.0f, maxDb = 0.0f;

    void draw(NanoVG& vg, float db) const
    {
        drawText(vg, bounds.x, bounds.y + 6.0f, 11.0f, theme::textDim(),
                 NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, label.c_str());
        char buf[32];
        if (db <= minDb + 0.5f)
            std::snprintf(buf, sizeof(buf), "-inf dB");
        else
            std::snprintf(buf, sizeof(buf), "%.1f dB", db);
        drawText(vg, bounds.x + bounds.w, bounds.y + 6.0f, 11.0f, theme::text(),
                 NanoVG::ALIGN_RIGHT | NanoVG::ALIGN_MIDDLE, buf);

        const float y = bounds.y + 16.0f, h = bounds.h - 16.0f;
        vg.beginPath();
        vg.roundedRect(bounds.x, y, bounds.w, h, h * 0.5f);
        vg.fillColor(theme::well());
        vg.fill();

        const float n = std::clamp((db - minDb) / (maxDb - minDb), 0.0f, 1.0f);
        if (n > 0.0f) {
            vg.beginPath();
            vg.roundedRect(bounds.x, y, std::max(h, bounds.w * n), h, h * 0.5f);
            vg.fillPaint(vg.linearGradient(bounds.x, 0, bounds.x + bounds.w, 0,
                                           theme::warmSoft(), theme::warm()));
            vg.fill();
        }
    }
};

// ---------------------------------------------------------------------------
// Scrolling history of a value (newest on the right), for activity and
// gain-reduction displays.

struct History {
    std::vector<float> values;
    int pos = 0;

    explicit History(int length = 240) : values(length, 0.0f) {}
    void push(float v)
    {
        values[pos] = v;
        pos = (pos + 1) % static_cast<int>(values.size());
    }
    // i = 0 is the oldest value
    float at(int i) const { return values[(pos + i) % values.size()]; }
    int size() const { return static_cast<int>(values.size()); }
};

// Draw a history as a filled area. With fromTop the area hangs down from
// the top edge (gain reduction); otherwise it rises from the bottom.
inline void drawHistory(NanoVG& vg, const Rect& r, const History& h, float minV, float maxV,
                        const Color& line, bool fromTop)
{
    const int n = h.size();
    auto y = [&](float v) {
        const float t = std::clamp((v - minV) / (maxV - minV), 0.0f, 1.0f);
        return fromTop ? r.y + t * r.h : r.y + r.h - t * r.h;
    };
    const float base = fromTop ? r.y : r.y + r.h;
    const float dx = r.w / static_cast<float>(n - 1);

    vg.beginPath();
    vg.moveTo(r.x, base);
    for (int i = 0; i < n; ++i)
        vg.lineTo(r.x + i * dx, y(h.at(i)));
    vg.lineTo(r.x + r.w, base);
    vg.closePath();
    Color top = line, bottom = line;
    top.alpha = fromTop ? 0.05f : 0.30f;
    bottom.alpha = fromTop ? 0.30f : 0.02f;
    vg.fillPaint(vg.linearGradient(0, r.y, 0, r.y + r.h, fromTop ? bottom : top, fromTop ? top : bottom));
    vg.fill();

    vg.beginPath();
    vg.moveTo(r.x, y(h.at(0)));
    for (int i = 1; i < n; ++i)
        vg.lineTo(r.x + i * dx, y(h.at(i)));
    vg.strokeColor(line);
    vg.strokeWidth(1.6f);
    vg.stroke();
}

} // namespace mend::ui
