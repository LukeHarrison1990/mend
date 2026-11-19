// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
//
// The Audio settings panel shared by the standalone apps and the editor:
// input and output devices, input channels, sample rate and buffer size.
// It draws over the whole window and takes all mouse input while open.
// The owner supplies the state and applies changes (each change applies
// straight away, like most audio apps).
#pragma once

#include "mend/ui/Widgets.hpp"

#include <chrono>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace mend::ui {

struct AudioPanelState {
    std::vector<std::string> inputs, outputs;
    std::string defaultInput, defaultOutput, backend;

    bool inputOn = false;
    std::string input, output; // empty = system default
    int inputLeft = 1, inputRight = 2;
    unsigned sampleRate = 0; // 0 = device's own
    unsigned bufferSize = 256;

    bool running = false, inputActive = false;
    std::string activeInput, activeOutput;
    unsigned inputChannels = 0, activeSampleRate = 0, activeBufferSize = 0;
    double latencyMs = 0.0;
    std::string note;
    std::string status; // replaces the "Running at..." line when set
};

class AudioPanel {
public:
    // Fill in the state; `listDevices` asks for the device lists too.
    std::function<AudioPanelState(bool listDevices)> getState;
    // Use the choice fields of a state.
    std::function<void(const AudioPanelState&)> apply;
    bool showInput = true;      // the editor only plays
    bool showSampleRate = true; // the editor plays at each file's rate
    std::string title = "Audio settings";
    std::string footnote;       // e.g. "Shared by all Mend apps."

    bool isOpen() const { return open_; }
    void open()
    {
        open_ = true;
        list_ = -1;
        refresh(true);
    }
    void close()
    {
        open_ = false;
        list_ = -1;
    }

    // Call at the UI's idle rate: refreshes the device lists now and then.
    // Returns true if the panel should be repainted.
    bool idle()
    {
        if (!open_)
            return false;
        const auto now = std::chrono::steady_clock::now();
        if (now - lastList_ > std::chrono::milliseconds(2000) && list_ < 0) {
            refresh(true);
            return true;
        }
        return false;
    }
    void refresh(bool listDevices)
    {
        if (!getState)
            return;
        AudioPanelState s = getState(listDevices);
        if (!listDevices) { // keep the lists we have
            s.inputs = st_.inputs;
            s.outputs = st_.outputs;
            s.defaultInput = st_.defaultInput;
            s.defaultOutput = st_.defaultOutput;
            s.backend = st_.backend;
        } else {
            lastList_ = std::chrono::steady_clock::now();
        }
        st_ = std::move(s);
    }

    // Everything is in the caller's layout units; the panel covers W x H.
    void draw(NanoVG& vg, float W, float H)
    {
        if (!open_)
            return;
        W_ = W;
        H_ = H;
        buildRows();

        vg.beginPath();
        vg.rect(0, 0, W, H);
        vg.fillColor(Color(0, 0, 0, 0.6f));
        vg.fill();
        drawPanel(vg, box_);

        drawText(vg, box_.x + 24, box_.y + 30, 16.0f, theme::text(), NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE,
                 title.c_str());
        if (!st_.backend.empty())
            drawText(vg, box_.x + box_.w - 56, box_.y + 30, 11.0f, theme::textFaint(),
                     NanoVG::ALIGN_RIGHT | NanoVG::ALIGN_MIDDLE, st_.backend.c_str());
        // close cross
        closeBox_ = { box_.x + box_.w - 44, box_.y + 14, 30, 30 };
        vg.beginPath();
        vg.moveTo(closeBox_.cx() - 6, closeBox_.cy() - 6);
        vg.lineTo(closeBox_.cx() + 6, closeBox_.cy() + 6);
        vg.moveTo(closeBox_.cx() + 6, closeBox_.cy() - 6);
        vg.lineTo(closeBox_.cx() - 6, closeBox_.cy() + 6);
        vg.strokeColor(hoverClose_ ? theme::text() : theme::textDim());
        vg.strokeWidth(1.6f);
        vg.stroke();

        for (size_t i = 0; i < rows_.size(); ++i)
            drawRow(vg, rows_[i], static_cast<int>(i) == list_);

        // status
        float y = statusY_;
        std::string running;
        if (!st_.status.empty()) {
            running = st_.status;
        } else if (st_.running || st_.activeSampleRate > 0) {
            char buf[160];
            std::snprintf(buf, sizeof(buf), "Running at %s  \xc2\xb7  %u-sample buffer  \xc2\xb7  about %.0f ms",
                          rateName(st_.activeSampleRate).c_str(), st_.activeBufferSize, st_.latencyMs);
            running = buf;
        } else {
            running = "Audio isn't running.";
        }
        drawText(vg, box_.x + 24, y, 12.0f, theme::textDim(), NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE,
                 running.c_str());
        y += 12;
        if (!st_.note.empty()) {
            vg.fontSize(12.0f);
            vg.fillColor(theme::warm());
            vg.textAlign(NanoVG::ALIGN_LEFT | NanoVG::ALIGN_TOP);
            vg.textBox(box_.x + 24, y, box_.w - 150, st_.note.c_str(), nullptr);
        } else if (!footnote.empty()) {
            drawText(vg, box_.x + 24, y + 8, 11.0f, theme::textFaint(), NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE,
                     footnote.c_str());
        }

        // Done
        done_ = { box_.x + box_.w - 112, box_.y + box_.h - 46, 92, 30 };
        vg.beginPath();
        vg.roundedRect(done_.x, done_.y, done_.w, done_.h, 15);
        vg.fillColor(hoverDone_ ? theme::accent() : theme::accentSoft());
        vg.fill();
        drawText(vg, done_.cx(), done_.cy() + 0.5f, 13.0f, hoverDone_ ? theme::background() : theme::accent(),
                 NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE, "Done");

        if (list_ >= 0 && list_ < static_cast<int>(rows_.size()))
            drawList(vg, rows_[static_cast<size_t>(list_)]);
    }

    bool mouseDown(double x, double y)
    {
        if (!open_)
            return false;
        if (list_ >= 0 && list_ < static_cast<int>(rows_.size())) {
            const Row& row = rows_[static_cast<size_t>(list_)];
            const int pick = listHit(row, x, y);
            list_ = -1;
            if (pick >= 0 && pick != row.current && row.pick)
                row.pick(pick);
            return true;
        }
        if (closeBox_.contains(x, y) || done_.contains(x, y)) {
            close();
            return true;
        }
        for (size_t i = 0; i < rows_.size(); ++i) {
            if (rows_[i].enabled && rows_[i].box.contains(x, y)) {
                list_ = static_cast<int>(i);
                listScroll_ = 0;
                hoverItem_ = -1;
                return true;
            }
        }
        if (!box_.contains(x, y))
            close(); // click outside
        return true;
    }

    bool motion(double x, double y)
    {
        if (!open_)
            return false;
        hoverClose_ = closeBox_.contains(x, y);
        hoverDone_ = done_.contains(x, y);
        if (list_ >= 0 && list_ < static_cast<int>(rows_.size()))
            hoverItem_ = listHit(rows_[static_cast<size_t>(list_)], x, y);
        return true;
    }

    bool scroll(double x, double y, double dy)
    {
        if (!open_)
            return false;
        if (list_ >= 0 && list_ < static_cast<int>(rows_.size())) {
            const Row& row = rows_[static_cast<size_t>(list_)];
            listScroll_ = std::clamp(listScroll_ + (dy > 0 ? -1 : 1), 0,
                                     std::max(0, static_cast<int>(row.choices.size()) - row.visible));
            hoverItem_ = listHit(row, x, y);
        }
        return true;
    }

    // Escape: close the open list, then the panel.
    bool escape()
    {
        if (!open_)
            return false;
        if (list_ >= 0)
            list_ = -1;
        else
            close();
        return true;
    }

private:
    struct Row {
        std::string label, value;
        std::vector<std::string> choices;
        int current = -1;
        bool enabled = true;
        std::function<void(int)> pick;
        Rect box;
        mutable Rect listBox;
        mutable int visible = 0;
    };

    static std::string rateName(unsigned r)
    {
        if (r == 0)
            return "?";
        char buf[32];
        if (r % 1000 == 0)
            std::snprintf(buf, sizeof(buf), "%u kHz", r / 1000);
        else
            std::snprintf(buf, sizeof(buf), "%.1f kHz", r / 1000.0);
        return buf;
    }

    void change(const std::function<void(AudioPanelState&)>& f)
    {
        AudioPanelState s = st_;
        f(s);
        if (apply)
            apply(s);
        refresh(false);
    }

    void buildRows()
    {
        rows_.clear();
        // Input
        if (showInput) {
            Row r;
            r.label = "Input";
            r.choices.push_back("Off");
            r.choices.push_back(defaultName(st_.defaultInput));
            for (const auto& n : st_.inputs)
                r.choices.push_back(n);
            if (!st_.inputOn)
                r.current = 0;
            else if (st_.input.empty())
                r.current = 1;
            else
                r.current = indexOf(st_.inputs, st_.input, 2);
            if (st_.inputOn && !st_.input.empty() && r.current < 0) {
                r.choices.push_back(st_.input + "  (not connected)");
                r.current = static_cast<int>(r.choices.size()) - 1;
            }
            const std::vector<std::string> inputs = st_.inputs;
            r.pick = [this, inputs](int i) {
                change([&](AudioPanelState& s) {
                    s.inputOn = i != 0;
                    if (i == 1)
                        s.input.clear();
                    else if (i >= 2 && i - 2 < static_cast<int>(inputs.size())) {
                        if (s.input != inputs[static_cast<size_t>(i - 2)]) { // a new device: start on its first inputs
                            s.inputLeft = 1;
                            s.inputRight = 2;
                        }
                        s.input = inputs[static_cast<size_t>(i - 2)];
                    }
                });
            };
            rows_.push_back(std::move(r));

            Row c;
            c.label = "Input channels";
            const unsigned n = st_.inputActive && st_.inputChannels > 0 ? st_.inputChannels : 2;
            struct Ch {
                int l, r;
            };
            std::vector<Ch> map;
            for (int i = 1; i + 1 <= static_cast<int>(n); i += 2) {
                c.choices.push_back("Inputs " + std::to_string(i) + " + " + std::to_string(i + 1));
                map.push_back({ i, i + 1 });
            }
            for (int i = 1; i <= static_cast<int>(n); ++i) {
                c.choices.push_back("Input " + std::to_string(i) + (n == 1 ? " (mono)" : " (mono, to both sides)"));
                map.push_back({ i, i });
            }
            for (size_t i = 0; i < map.size(); ++i)
                if (map[i].l == st_.inputLeft && map[i].r == st_.inputRight)
                    c.current = static_cast<int>(i);
            if (c.current < 0 && !map.empty())
                c.current = 0;
            c.enabled = st_.inputOn && st_.inputActive && n > 1;
            if (st_.inputActive && n == 1)
                c.value = "Input 1 (mono)";
            c.pick = [this, map](int i) {
                if (i < 0 || i >= static_cast<int>(map.size()))
                    return;
                change([&](AudioPanelState& s) {
                    s.inputLeft = map[static_cast<size_t>(i)].l;
                    s.inputRight = map[static_cast<size_t>(i)].r;
                });
            };
            rows_.push_back(std::move(c));
        }
        // Output
        {
            Row r;
            r.label = "Output";
            r.choices.push_back(defaultName(st_.defaultOutput));
            for (const auto& n : st_.outputs)
                r.choices.push_back(n);
            r.current = st_.output.empty() ? 0 : indexOf(st_.outputs, st_.output, 1);
            if (!st_.output.empty() && r.current < 0) {
                r.choices.push_back(st_.output + "  (not connected)");
                r.current = static_cast<int>(r.choices.size()) - 1;
            }
            const std::vector<std::string> outputs = st_.outputs;
            r.pick = [this, outputs](int i) {
                change([&](AudioPanelState& s) {
                    if (i == 0)
                        s.output.clear();
                    else if (i - 1 < static_cast<int>(outputs.size()))
                        s.output = outputs[static_cast<size_t>(i - 1)];
                });
            };
            rows_.push_back(std::move(r));
        }
        // Sample rate
        if (showSampleRate) {
            static const unsigned rates[] = { 0, 44100, 48000, 88200, 96000 };
            Row r;
            r.label = "Sample rate";
            for (unsigned rt : rates) {
                if (rt == 0)
                    r.choices.push_back(st_.sampleRate == 0 && st_.activeSampleRate > 0
                                            ? "Device's own (" + rateName(st_.activeSampleRate) + ")"
                                            : "Device's own");
                else
                    r.choices.push_back(rateName(rt));
                if (rt == st_.sampleRate)
                    r.current = static_cast<int>(r.choices.size()) - 1;
            }
            r.pick = [this](int i) {
                change([&](AudioPanelState& s) { s.sampleRate = rates[i]; });
            };
            rows_.push_back(std::move(r));
        }
        // Buffer size
        {
            static const unsigned sizes[] = { 64, 128, 256, 512, 1024, 2048 };
            Row r;
            r.label = "Buffer size";
            const double rate = st_.activeSampleRate > 0 ? st_.activeSampleRate : 48000.0;
            for (unsigned b : sizes) {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%u samples  (%.1f ms)", b, 1000.0 * b / rate);
                r.choices.push_back(buf);
                if (b == st_.bufferSize)
                    r.current = static_cast<int>(r.choices.size()) - 1;
            }
            r.pick = [this](int i) {
                change([&](AudioPanelState& s) { s.bufferSize = sizes[i]; });
            };
            rows_.push_back(std::move(r));
        }

        // layout
        const float rowH = 46.0f;
        const float boxW = std::min(W_ - 40.0f, 640.0f);
        const float boxH = 60.0f + rowH * static_cast<float>(rows_.size()) + 96.0f;
        box_ = { (W_ - boxW) * 0.5f, std::max(12.0f, (H_ - boxH) * 0.5f), boxW, std::min(boxH, H_ - 24.0f) };
        float y = box_.y + 62;
        for (Row& r : rows_) {
            r.box = { box_.x + 170, y, box_.w - 194, 32 };
            if (r.value.empty())
                r.value = r.current >= 0 && r.current < static_cast<int>(r.choices.size())
                    ? r.choices[static_cast<size_t>(r.current)]
                    : std::string();
            y += rowH;
        }
        statusY_ = y + 10;
    }

    static int indexOf(const std::vector<std::string>& v, const std::string& n, int offset)
    {
        for (size_t i = 0; i < v.size(); ++i)
            if (v[i] == n)
                return static_cast<int>(i) + offset;
        return -1;
    }
    static std::string defaultName(const std::string& def)
    {
        return def.empty() ? std::string("System default") : "System default  (" + def + ")";
    }

    static std::string fit(NanoVG& vg, const std::string& s, float maxW)
    {
        DGL_NAMESPACE::Rectangle<float> b;
        if (vg.textBounds(0, 0, s.c_str(), nullptr, b) <= maxW)
            return s;
        std::string t = s;
        while (!t.empty()) {
            // drop whole UTF-8 characters
            do
                t.pop_back();
            while (!t.empty() && (static_cast<unsigned char>(t.back()) & 0xC0) == 0x80);
            if (!t.empty())
                t.pop_back();
            const std::string c = t + "\xe2\x80\xa6";
            if (vg.textBounds(0, 0, c.c_str(), nullptr, b) <= maxW)
                return c;
        }
        return "\xe2\x80\xa6";
    }

    void drawRow(NanoVG& vg, const Row& r, bool openNow)
    {
        drawText(vg, box_.x + 24, r.box.cy(), 12.5f, r.enabled ? theme::textDim() : theme::textFaint(),
                 NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, r.label.c_str());
        const Rect& b = r.box;
        vg.beginPath();
        vg.roundedRect(b.x, b.y, b.w, b.h, 6);
        vg.fillColor(theme::well());
        vg.fill();
        vg.strokeColor(openNow ? theme::accent() : theme::panelEdge());
        vg.strokeWidth(1.0f);
        vg.stroke();
        vg.fontSize(13.0f);
        const std::string v = fit(vg, r.value, b.w - 44);
        drawText(vg, b.x + 10, b.cy() + 0.5f, 13.0f, r.enabled ? theme::text() : theme::textFaint(),
                 NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, v.c_str());
        if (!r.enabled)
            return;
        vg.beginPath();
        vg.moveTo(b.x + b.w - 20, b.cy() - 2.5f);
        vg.lineTo(b.x + b.w - 15, b.cy() + 2.5f);
        vg.lineTo(b.x + b.w - 10, b.cy() - 2.5f);
        vg.strokeColor(theme::textDim());
        vg.strokeWidth(1.6f);
        vg.stroke();
    }

    static constexpr float kItemH = 26.0f;

    void layoutList(const Row& r) const
    {
        const int n = static_cast<int>(r.choices.size());
        const float maxH = H_ - 24.0f;
        r.visible = std::max(1, std::min(n, static_cast<int>((maxH - 8) / kItemH)));
        const float h = r.visible * kItemH + 8;
        float y = r.box.y + r.box.h + 2;
        if (y + h > H_ - 12)
            y = std::max(12.0f, H_ - 12 - h);
        r.listBox = { r.box.x, y, r.box.w, h };
    }

    int listHit(const Row& r, double x, double y) const
    {
        layoutList(r);
        if (!r.listBox.contains(x, y))
            return -1;
        const int i = static_cast<int>((y - r.listBox.y - 4) / kItemH);
        if (i < 0 || i >= r.visible)
            return -1;
        const int idx = i + listScroll_;
        return idx < static_cast<int>(r.choices.size()) ? idx : -1;
    }

    void drawList(NanoVG& vg, const Row& r)
    {
        layoutList(r);
        const Rect& lb = r.listBox;
        vg.beginPath();
        vg.roundedRect(lb.x, lb.y, lb.w, lb.h, 6);
        vg.fillColor(theme::panel());
        vg.fill();
        vg.strokeColor(theme::accent());
        vg.strokeWidth(1.0f);
        vg.stroke();
        vg.fontSize(13.0f);
        for (int i = 0; i < r.visible; ++i) {
            const int idx = i + listScroll_;
            if (idx >= static_cast<int>(r.choices.size()))
                break;
            const Rect it { lb.x + 4, lb.y + 4 + kItemH * static_cast<float>(i), lb.w - 8, kItemH };
            if (idx == r.current || idx == hoverItem_) {
                vg.beginPath();
                vg.roundedRect(it.x, it.y + 1, it.w, it.h - 2, 4);
                vg.fillColor(idx == r.current ? theme::accentSoft() : Color(255, 255, 255, 0.06f));
                vg.fill();
            }
            const std::string t = fit(vg, r.choices[static_cast<size_t>(idx)], it.w - 16);
            drawText(vg, it.x + 8, it.cy() + 0.5f, 13.0f, idx == r.current ? theme::accent() : theme::text(),
                     NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE, t.c_str());
        }
        if (static_cast<int>(r.choices.size()) > r.visible) { // scroll hint
            const float frac = static_cast<float>(r.visible) / static_cast<float>(r.choices.size());
            const float th = (lb.h - 8) * frac;
            const float ty = lb.y + 4 + (lb.h - 8 - th) * static_cast<float>(listScroll_)
                    / static_cast<float>(r.choices.size() - static_cast<size_t>(r.visible));
            vg.beginPath();
            vg.roundedRect(lb.x + lb.w - 5, ty, 3, th, 1.5f);
            vg.fillColor(theme::textFaint());
            vg.fill();
        }
    }

    bool open_ = false;
    AudioPanelState st_;
    std::vector<Row> rows_;
    int list_ = -1, hoverItem_ = -1, listScroll_ = 0;
    bool hoverClose_ = false, hoverDone_ = false;
    float W_ = 0, H_ = 0, statusY_ = 0;
    Rect box_, closeBox_, done_;
    std::chrono::steady_clock::time_point lastList_ {};
};

} // namespace mend::ui
