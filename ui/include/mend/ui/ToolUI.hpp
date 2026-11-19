// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
//
// Base class for every Mend tool's plugin UI. It provides the common
// frame (header with wordmark, tool name and status pill; a main display
// panel; a controls panel), parameter bookkeeping, and all knob/toggle
// interaction. A tool's UI adds its controls in its constructor and
// draws its own display in drawDisplay().
//
// Include this from a plugin's UI source (it needs that plugin's
// DistrhoPluginInfo.h, like any DPF UI).
#pragma once

#include "DistrhoStandaloneUtils.hpp"
#include "DistrhoUI.hpp"
#include "mend/ui/AudioPanel.hpp"
#include "mend/ui/LogoPng.hpp"
#include "mend/ui/Widgets.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <cstdio>
#include <string>
#include <vector>

START_NAMESPACE_DISTRHO

class MendToolUI : public UI {
public:
    static constexpr float kW = DISTRHO_UI_DEFAULT_WIDTH;
    static constexpr float kH = DISTRHO_UI_DEFAULT_HEIGHT;

    MendToolUI(const char* toolName, std::vector<float> defaults)
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
        , toolName_(toolName)
        , values_(std::move(defaults))
    {
        loadSharedResources();

        // We do our own scaling instead of DPF's automatic scaling: DPF's
        // renders at 100% and stretches the result, which blurs text and
        // edges at any other display scale. Here the window is created at
        // the real pixel size and every frame is drawn at full resolution
        // with a vector scale applied (see onNanoDisplay).
        applyScaleFactor(getScaleFactor());
        display_ = { 20, 66, kW - 40, 214 };
        controlsPanel_ = { 20, 294, kW - 40, kH - 314 };

        // Audio settings (standalone apps only)
        audio_.footnote = "These settings are shared by all the Mend apps.";
        audio_.getState = [](bool list) {
            const StandaloneAudioState a = getStandaloneAudioState(list);
            mend::ui::AudioPanelState s;
            s.inputs = a.inputs;
            s.outputs = a.outputs;
            s.defaultInput = a.defaultInput;
            s.defaultOutput = a.defaultOutput;
            s.backend = a.backend;
            s.inputOn = a.inputOn;
            s.input = a.input;
            s.output = a.output;
            s.inputLeft = a.inputLeft;
            s.inputRight = a.inputRight;
            s.sampleRate = a.sampleRate;
            s.bufferSize = a.bufferSize;
            s.running = a.running;
            s.inputActive = a.inputActive;
            s.activeInput = a.activeInput;
            s.activeOutput = a.activeOutput;
            s.inputChannels = a.inputChannels;
            s.activeSampleRate = a.activeSampleRate;
            s.activeBufferSize = a.activeBufferSize;
            s.latencyMs = a.latencyMs;
            s.note = a.note;
            return s;
        };
        audio_.apply = [](const mend::ui::AudioPanelState& s) {
            StandaloneAudioState a;
            a.inputOn = s.inputOn;
            a.input = s.input;
            a.output = s.output;
            a.inputLeft = s.inputLeft;
            a.inputRight = s.inputRight;
            a.sampleRate = s.sampleRate;
            a.bufferSize = s.bufferSize;
            setStandaloneAudio(a);
        };
    }

protected:
    using Rect = mend::ui::Rect;
    using Knob = mend::ui::Knob;
    using Toggle = mend::ui::Toggle;
    using Meter = mend::ui::Meter;

    // ---- for subclasses ---------------------------------------------------

    static std::string fmt(const char* f, float v)
    {
        char buf[48];
        std::snprintf(buf, sizeof(buf), f, v);
        return buf;
    }

    float value(unsigned index) const { return index < values_.size() ? values_[index] : 0.0f; }
    bool on(unsigned index) const { return value(index) > 0.5f; }

    // Knobs fill slots left to right along the controls panel.
    Knob& addKnob(unsigned param, const char* label, float min, float max, bool logarithmic, bool integer,
                  std::function<std::string(float)> format)
    {
        Knob k;
        const float w = 112, h = controlsPanel_.h - 20;
        k.bounds = { controlsPanel_.x + 14 + w * static_cast<float>(knobs_.size()), controlsPanel_.y + 10, w, h };
        k.param = param;
        k.label = label;
        k.min = min;
        k.max = max;
        k.def = values_[param];
        k.value = values_[param];
        k.logarithmic = logarithmic;
        k.integer = integer;
        k.format = std::move(format);
        knobs_.push_back(k);
        return knobs_.back();
    }

    // Rearrange the knobs into a grid (for tools with many controls; give
    // the plugin a taller DISTRHO_UI_DEFAULT_HEIGHT to make room).
    void layoutKnobGrid(int columns, float rowHeight = 104.0f)
    {
        const float w = 112;
        for (size_t i = 0; i < knobs_.size(); ++i) {
            const int r = static_cast<int>(i) / columns, c = static_cast<int>(i) % columns;
            knobs_[i].bounds = { controlsPanel_.x + 14 + w * c, controlsPanel_.y + 8 + rowHeight * r, w, rowHeight - 6 };
        }
    }
    // Put knob `index` (in the order added) at a grid cell.
    void placeKnob(size_t index, int column, int row, float rowHeight = 100.0f)
    {
        if (index < knobs_.size())
            knobs_[index].bounds = { controlsPanel_.x + 14 + 112.0f * column, controlsPanel_.y + 8 + rowHeight * row, 112,
                                     rowHeight - 6 };
    }
    double sampleRateForDisplay() const { return getSampleRate(); }

    // Toggles stack down the right of the controls panel.
    void addToggle(unsigned param, const char* label, std::function<void(bool)> onChange = {})
    {
        const float x = controlsPanel_.x + controlsPanel_.w - 236;
        Toggle t;
        t.bounds = { x, controlsPanel_.y + 18 + 38.0f * static_cast<float>(toggles_.size()), 216, 30 };
        t.param = param;
        t.label = label;
        t.onChange = std::move(onChange);
        toggles_.push_back(t);
    }

    // One meter, under the toggles.
    void setMeter(unsigned param, const char* label, float minDb, float maxDb)
    {
        const float x = controlsPanel_.x + controlsPanel_.w - 236;
        meter_ = { { x, controlsPanel_.y + 96, 216, 24 }, label, minDb, maxDb };
        meterParam_ = static_cast<int>(param);
    }

    // A drop-down list of named choices for an integer parameter. The list
    // opens over the whole window; `groups` (optional, one per choice)
    // puts headings between runs of choices.
    struct Selector {
        Rect bounds;
        unsigned param = 0;
        std::string label;
        std::vector<std::string> names, groups;
        std::function<void(int)> onChange;
    };
    Selector& addSelector(unsigned param, const char* label, const Rect& bounds, std::vector<std::string> names,
                          std::vector<std::string> groups = {}, std::function<void(int)> onChange = {})
    {
        Selector s;
        s.bounds = bounds;
        s.param = param;
        s.label = label;
        s.names = std::move(names);
        s.groups = std::move(groups);
        s.onChange = std::move(onChange);
        selectors_.push_back(std::move(s));
        return selectors_.back();
    }
    Knob& knob(size_t i) { return knobs_[i]; }

    // Change a parameter from the UI and tell the host.
    void setParam(unsigned index, float v, bool gesture = true)
    {
        if (gesture)
            editParameter(index, true);
        values_[index] = v;
        for (auto& k : knobs_)
            if (k.param == index)
                k.value = v;
        setParameterValue(index, v);
        if (gesture)
            editParameter(index, false);
        repaint();
    }

    // Status pill text and colour; default shows nothing.
    virtual std::string statusText(mend::ui::Color& colour)
    {
        (void)colour;
        return {};
    }
    // The tool's main display, inside the top panel.
    virtual void drawDisplay(const Rect& r) = 0;
    // Called at the UI's idle rate, e.g. to push into a History.
    virtual void idleTick() {}
    // Called when a parameter arrives from the host.
    virtual void parameterArrived(uint32_t, float) {}

    // ---- framework --------------------------------------------------------

    void parameterChanged(uint32_t index, float v) override
    {
        if (index >= values_.size() || values_[index] == v)
            return;
        values_[index] = v;
        for (auto& k : knobs_)
            if (k.param == index)
                k.value = v;
        parameterArrived(index, v);
        repaint();
    }

    void uiIdle() override
    {
        if (standaloneAudioIdle()) // a device came or went
            audio_.refresh(true);
        audio_.idle();
        idleTick();
        repaint();
    }

    void onNanoDisplay() override
    {
        using namespace mend::ui;
        fontFace(NANOVG_DEJAVU_SANS_TTF);

        beginPath();
        rect(0, 0, getWindow().getWidth(), getWindow().getHeight());
        fillColor(theme::background());
        fill();

        // Everything below is laid out in kW x kH units and drawn crisply at
        // the window's real resolution.
        save();
        const float s = static_cast<float>(layoutScale());
        scale(s, s);

        drawHeader();

        drawPanel(*this, display_);
        save();
        scissor(display_.x, display_.y, display_.w, display_.h);
        drawDisplay(display_);
        restore();

        drawPanel(*this, controlsPanel_);
        for (auto& k : knobs_) {
            k.dimmed = k.isDimmed ? k.isDimmed() : false;
            k.automatic = k.isAutomatic ? k.isAutomatic() : false;
            float shown = k.displayValue ? k.displayValue() : k.value;
            if (k.automatic) { // glide towards the detected value, in knob travel
                const float target = k.toNorm(shown);
                k.eased = k.easedValid ? k.eased + 0.25f * (target - k.eased) : target;
                k.easedValid = true;
                shown = std::abs(k.eased - target) < 1e-4f ? shown : k.fromNorm(k.eased);
            } else {
                k.easedValid = false;
            }
            k.draw(*this, shown);
        }
        for (auto& t : toggles_) {
            t.on = on(t.param);
            t.draw(*this);
        }
        if (meterParam_ >= 0)
            meter_.draw(*this, value(static_cast<unsigned>(meterParam_)));
        for (const auto& sel : selectors_)
            drawSelector(sel);
        if (openSelector_ >= 0)
            drawSelectorList(selectors_[static_cast<size_t>(openSelector_)]);
        audio_.draw(*this, kW, kH);
        restore();
    }

    void drawSelector(const Selector& sel)
    {
        using namespace mend::ui;
        const Rect& b = sel.bounds;
        drawText(*this, b.x + 2, b.y - 9, 11.0f, theme::textDim(), ALIGN_LEFT | ALIGN_MIDDLE, sel.label.c_str());
        beginPath();
        roundedRect(b.x, b.y, b.w, b.h, 6);
        fillColor(theme::well());
        fill();
        strokeColor(theme::panelEdge());
        strokeWidth(1.0f);
        stroke();
        const int i = static_cast<int>(std::lround(value(sel.param)));
        const char* name = i >= 0 && i < static_cast<int>(sel.names.size()) ? sel.names[static_cast<size_t>(i)].c_str() : "";
        drawText(*this, b.x + 10, b.cy() + 0.5f, 13.0f, theme::text(), ALIGN_LEFT | ALIGN_MIDDLE, name);
        beginPath(); // chevron
        moveTo(b.x + b.w - 20, b.cy() - 2.5f);
        lineTo(b.x + b.w - 15, b.cy() + 2.5f);
        lineTo(b.x + b.w - 10, b.cy() - 2.5f);
        strokeColor(theme::textDim());
        strokeWidth(1.6f);
        stroke();
    }

    // Layout of the open list: item rectangles (and heading positions).
    struct ListItem {
        Rect r;
        int index; // -1: heading
        std::string text;
    };
    std::vector<ListItem> selectorLayout(const Selector& sel) const
    {
        std::vector<ListItem> items;
        const float x0 = 30, y0 = 70, colW = 180, rowH = 22, bottom = kH - 24;
        float x = x0, y = y0;
        std::string group;
        for (size_t i = 0; i < sel.names.size(); ++i) {
            const std::string g = i < sel.groups.size() ? sel.groups[i] : std::string();
            const bool heading = !g.empty() && g != group;
            if (y + rowH * (heading ? 2.0f : 1.0f) > bottom) {
                x += colW;
                y = y0;
            }
            if (heading) {
                items.push_back({ { x, y, colW - 8, rowH }, -1, g });
                y += rowH;
                group = g;
            }
            items.push_back({ { x, y, colW - 8, rowH }, static_cast<int>(i), sel.names[i] });
            y += rowH;
        }
        return items;
    }

    void drawSelectorList(const Selector& sel)
    {
        using namespace mend::ui;
        beginPath();
        rect(0, 0, kW, kH);
        fillColor(Color(0, 0, 0, 0.55f));
        fill();
        drawPanel(*this, { 20, 40, kW - 40, kH - 56 });
        drawText(*this, 34, 56, 12.0f, theme::textDim(), ALIGN_LEFT | ALIGN_MIDDLE, sel.label.c_str());
        const int current = static_cast<int>(std::lround(value(sel.param)));
        for (const auto& it : selectorLayout(sel)) {
            if (it.index < 0) {
                drawText(*this, it.r.x + 6, it.r.cy() + 2, 10.0f, theme::textFaint(), ALIGN_LEFT | ALIGN_MIDDLE,
                         it.text.c_str());
                continue;
            }
            if (it.index == current || it.index == hoverItem_) {
                beginPath();
                roundedRect(it.r.x, it.r.y + 1, it.r.w, it.r.h - 2, 4);
                fillColor(it.index == current ? theme::accentSoft() : Color(255, 255, 255, 0.06f));
                fill();
            }
            drawText(*this, it.r.x + 8, it.r.cy() + 0.5f, 12.5f, it.index == current ? theme::accent() : theme::text(),
                     ALIGN_LEFT | ALIGN_MIDDLE, it.text.c_str());
        }
    }

    // Window pixels per layout unit. Measured from the real window, not the
    // widget: in some hosts (Audacity 4 and others on Windows) the window is
    // sized in physical pixels while DPF still reports the widget at its
    // design size. Mouse events arrive in window pixels, and onDisplay below
    // renders at window resolution, so both use this one factor.
    double layoutScale() const
    {
        const double sx = getWindow().getWidth() / static_cast<double>(kW);
        const double sy = getWindow().getHeight() / static_cast<double>(kH);
        const double s = sx < sy ? sx : sy;
        return s > 0.0 ? s : 1.0;
    }

    // Start every NanoVG frame at the window's real pixel size (DPF would
    // use the widget size, which can lag behind and be stretched, blurring
    // the UI). We have no child widgets, so nothing else is needed here.
    void onDisplay() override
    {
        NanoVG::beginFrame(getWindow().getWidth(), getWindow().getHeight());
        onNanoDisplay();
        NanoVG::endFrame();
    }

    void applyScaleFactor(double sf)
    {
        if (sf <= 0.0)
            sf = 1.0;
        const uint w = static_cast<uint>(kW * sf + 0.5), h = static_cast<uint>(kH * sf + 0.5);
        setGeometryConstraints(w, h, true); // minimum size, keep aspect ratio
        if (getWidth() != w || getHeight() != h)
            setSize(w, h);
    }

    // e.g. the window moved to a monitor with a different scale
    void uiScaleFactorChanged(double sf) override { applyScaleFactor(sf); }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != kMouseButtonLeft)
            return audio_.isOpen();
        if (audio_.isOpen()) {
            if (ev.press)
                audio_.mouseDown(ev.pos.getX() / layoutScale(), ev.pos.getY() / layoutScale());
            repaint();
            return true;
        }
        if (!ev.press) {
            if (dragging_ >= 0) {
                editParameter(knobs_[dragging_].param, false);
                dragging_ = -1;
                return true;
            }
            return false;
        }
        const double x = ev.pos.getX() / layoutScale(), y = ev.pos.getY() / layoutScale();
        if (openSelector_ >= 0) {
            Selector& sel = selectors_[static_cast<size_t>(openSelector_)];
            for (const auto& it : selectorLayout(sel)) {
                if (it.index >= 0 && it.r.contains(x, y)) {
                    setParam(sel.param, static_cast<float>(it.index));
                    if (sel.onChange)
                        sel.onChange(it.index);
                    break;
                }
            }
            openSelector_ = -1;
            repaint();
            return true;
        }
        for (size_t i = 0; i < selectors_.size(); ++i) {
            if (selectors_[i].bounds.contains(x, y)) {
                openSelector_ = static_cast<int>(i);
                hoverItem_ = -1;
                repaint();
                return true;
            }
        }
        if (showInputPill() && inputPill_.contains(x, y)) {
            if (supportsAudioDeviceSelection())
                audio_.open();
            else if (!isAudioInputEnabled())
                requestAudioInput();
            repaint();
            return true;
        }
        for (auto& t : toggles_) {
            if (t.bounds.contains(x, y)) {
                const bool now = !on(t.param);
                setParam(t.param, now ? 1.0f : 0.0f);
                if (t.onChange)
                    t.onChange(now);
                return true;
            }
        }
        for (size_t i = 0; i < knobs_.size(); ++i) {
            Knob& k = knobs_[i];
            if (!k.bounds.contains(x, y))
                continue;
            if (k.onGrab)
                k.onGrab();
            const auto now = std::chrono::steady_clock::now();
            const bool doubleClick = lastClickKnob_ == static_cast<int>(i)
                && now - lastClick_ < std::chrono::milliseconds(350);
            lastClick_ = now;
            lastClickKnob_ = static_cast<int>(i);
            if (doubleClick) {
                setParam(k.param, k.def);
                return true;
            }
            dragging_ = static_cast<int>(i);
            dragStartY_ = y;
            dragStartNorm_ = k.toNorm(k.value);
            editParameter(k.param, true);
            return true;
        }
        return false;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        if (audio_.isOpen()) {
            audio_.motion(ev.pos.getX() / layoutScale(), ev.pos.getY() / layoutScale());
            repaint();
            return true;
        }
        if (openSelector_ >= 0) {
            const double x = ev.pos.getX() / layoutScale(), y = ev.pos.getY() / layoutScale();
            int hover = -1;
            for (const auto& it : selectorLayout(selectors_[static_cast<size_t>(openSelector_)]))
                if (it.index >= 0 && it.r.contains(x, y))
                    hover = it.index;
            if (hover != hoverItem_) {
                hoverItem_ = hover;
                repaint();
            }
            return true;
        }
        if (dragging_ < 0)
            return false;
        Knob& k = knobs_[dragging_];
        const double sens = (ev.mod & kModifierShift) ? 1.0 / 1000.0 : 1.0 / 200.0;
        const double y = ev.pos.getY() / layoutScale();
        const float v = k.fromNorm(static_cast<float>(dragStartNorm_ + (dragStartY_ - y) * sens));
        if (v != k.value)
            setParam(k.param, v, false);
        return true;
    }

    bool onScroll(const ScrollEvent& ev) override
    {
        const double x = ev.pos.getX() / layoutScale(), y = ev.pos.getY() / layoutScale();
        if (audio_.isOpen()) {
            audio_.scroll(x, y, ev.delta.getY());
            repaint();
            return true;
        }
        if (openSelector_ >= 0)
            return true;
        for (auto& sel : selectors_) { // scroll through the choices
            if (!sel.bounds.contains(x, y))
                continue;
            const int n = static_cast<int>(sel.names.size());
            const int i = std::clamp(static_cast<int>(std::lround(value(sel.param))) + (ev.delta.getY() > 0 ? -1 : 1), 0, n - 1);
            setParam(sel.param, static_cast<float>(i));
            if (sel.onChange)
                sel.onChange(i);
            return true;
        }
        for (Knob& k : knobs_) {
            if (!k.bounds.contains(x, y))
                continue;
            if (k.isDimmed && k.isDimmed())
                return true;
            if (k.isAutomatic && k.isAutomatic() && k.onGrab)
                k.onGrab(); // take over from automatic control
            const float step = k.integer ? 1.0f / (k.max - k.min) : ((ev.mod & kModifierShift) ? 0.002f : 0.02f);
            setParam(k.param, k.fromNorm(k.toNorm(k.value) + static_cast<float>(ev.delta.getY()) * step));
            return true;
        }
        return false;
    }

    bool onKeyboard(const KeyboardEvent& ev) override
    {
        if (ev.press && ev.key == kKeyEscape) {
            if (audio_.escape()) {
                repaint();
                return true;
            }
            if (openSelector_ >= 0) {
                openSelector_ = -1;
                repaint();
                return true;
            }
        }
        return false;
    }

private:
    // Standalone apps: the header pill shows the audio input and opens the
    // Audio settings. (In plugin builds these calls are stubs returning false.)
    static bool showInputPill() { return isUsingNativeAudio() && supportsAudioInput(); }

    void drawInputPill()
    {
        using namespace mend::ui;
        if (!showInputPill())
            return;
        if (supportsAudioDeviceSelection()) {
            const StandaloneAudioState a = getStandaloneAudioState(false);
            const bool in = a.inputActive;
            std::string label = in ? a.activeInput : std::string("AUDIO INPUT OFF  \xc2\xb7  CLICK TO SET UP");
            fontSize(12.0f);
            DGL_NAMESPACE::Rectangle<float> b;
            const float maxText = kW - 250 - 268 - 46;
            if (in) { // device name, shortened to fit
                while (label.size() > 4 && textBounds(0, 0, label.c_str(), nullptr, b) > maxText - 34) {
                    do
                        label.pop_back();
                    while (!label.empty() && (static_cast<unsigned char>(label.back()) & 0xC0) == 0x80);
                }
                if (label != a.activeInput)
                    label += "\xe2\x80\xa6";
            }
            const float tw = textBounds(0, 0, label.c_str(), nullptr, b);
            const float w = std::min(maxText + 40, tw + (in ? 66.0f : 28.0f));
            inputPill_ = { 268, 18, w, 28 };
            beginPath();
            roundedRect(inputPill_.x, inputPill_.y, inputPill_.w, inputPill_.h, 14);
            fillColor(in ? theme::panel() : theme::warmSoft());
            fill();
            strokeColor(in ? theme::panelEdge() : theme::warm());
            strokeWidth(1.0f);
            stroke();
            if (in) {
                drawText(*this, inputPill_.x + 14, inputPill_.cy() + 0.5f, 10.5f, theme::accent(),
                         ALIGN_LEFT | ALIGN_MIDDLE, "IN");
                drawText(*this, inputPill_.x + 34, inputPill_.cy() + 0.5f, 12.0f, theme::text(),
                         ALIGN_LEFT | ALIGN_MIDDLE, label.c_str());
                // cog-like marker: three dots
                for (int i = 0; i < 3; ++i) {
                    beginPath();
                    circle(inputPill_.x + inputPill_.w - 22 + 5.0f * i, inputPill_.cy(), 1.4f);
                    fillColor(theme::textDim());
                    fill();
                }
            } else {
                drawText(*this, inputPill_.cx(), inputPill_.cy() + 0.5f, 12.0f, theme::warm(),
                         ALIGN_CENTER | ALIGN_MIDDLE, label.c_str());
            }
            return;
        }
        const bool enabled = isAudioInputEnabled();
        inputPill_ = { 268, 18, enabled ? 110.0f : 272.0f, 28 };
        beginPath();
        roundedRect(inputPill_.x, inputPill_.y, inputPill_.w, inputPill_.h, 14);
        fillColor(enabled ? theme::accentSoft() : theme::warmSoft());
        fill();
        strokeColor(enabled ? theme::accent() : theme::warm());
        strokeWidth(1.0f);
        stroke();
        drawText(*this, inputPill_.cx(), inputPill_.cy() + 0.5f, 12.0f, enabled ? theme::accent() : theme::warm(),
                 ALIGN_CENTER | ALIGN_MIDDLE,
                 enabled ? "INPUT ON" : "AUDIO INPUT OFF  \xc2\xb7  CLICK TO ENABLE");
    }

    void drawHeader()
    {
        using namespace mend::ui;
        if (!logo_.isValid())
            logo_ = createImageFromMemory(kLogoPng, kLogoPngSize, 0);
        if (logo_.isValid()) {
            beginPath();
            rect(20, 17, 30, 30);
            fillPaint(imagePattern(20, 17, 30, 30, 0.0f, logo_, 1.0f));
            fill();
        }
        fontSize(22.0f);
        fillColor(theme::text());
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(58, 32, "mend", nullptr);
        beginPath();
        circle(119, 38, 3.2f);
        fillColor(theme::accent());
        fill();
        drawText(*this, 134, 33, 15.0f, theme::textDim(), ALIGN_LEFT | ALIGN_MIDDLE, toolName_.c_str());
        drawInputPill();

        Color c = theme::textDim();
        const std::string status = statusText(c);
        if (status.empty())
            return;
        const float pw = 210, px = kW - 20 - pw, py = 18;
        beginPath();
        roundedRect(px, py, pw, 28, 14);
        fillColor(theme::panel());
        fill();
        strokeColor(theme::panelEdge());
        strokeWidth(1.0f);
        stroke();
        beginPath();
        circle(px + 16, py + 14, 4);
        fillColor(c);
        fill();
        drawText(*this, px + 28, py + 14.5f, 12.0f, c, ALIGN_LEFT | ALIGN_MIDDLE, status.c_str());
    }

    std::string toolName_;
    std::vector<float> values_;
    Rect display_, controlsPanel_;
    NanoImage logo_;
    std::vector<Knob> knobs_;
    std::vector<Toggle> toggles_;
    std::vector<Selector> selectors_;
    int openSelector_ = -1, hoverItem_ = -1;
    Meter meter_;
    int meterParam_ = -1;
    Rect inputPill_;
    mend::ui::AudioPanel audio_;

    int dragging_ = -1;
    double dragStartY_ = 0.0;
    float dragStartNorm_ = 0.0f;
    std::chrono::steady_clock::time_point lastClick_ {};
    int lastClickKnob_ = -1;
};

END_NAMESPACE_DISTRHO
