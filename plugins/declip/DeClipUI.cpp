// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors

#include "mend/ui/ToolUI.hpp"
#include "DeClipParams.hpp"

#include <cmath>

START_NAMESPACE_DISTRHO

using namespace mend::ui;

class DeClipUI : public MendToolUI {
public:
    DeClipUI()
        : MendToolUI("De-clip", mend::defaultsOf(kDeClipParams))
    {
        const auto& p = kDeClipParams;
        Knob& t = addKnob(kThreshold, "THRESHOLD", p[kThreshold].min, p[kThreshold].max, false, false,
                          [](float v) { return fmt("%.2f dB", v); });
        // In auto mode the knob follows the detected clip level; grabbing it switches to manual.
        t.isAutomatic = [this] { return on(kAutoThreshold); };
        t.displayValue = [this] { return on(kAutoThreshold) ? value(kClipLevel) : value(kThreshold); };
        t.onGrab = [this] {
            if (on(kAutoThreshold)) {
                setParam(kAutoThreshold, 0.0f);
                setParam(kThreshold, std::clamp(value(kClipLevel), kDeClipParams[kThreshold].min, 0.0f));
            }
        };
        addKnob(kOutputGain, "OUTPUT GAIN", p[kOutputGain].min, p[kOutputGain].max, false, false,
                [](float v) { return fmt("%+.1f dB", v); });
        addToggle(kAutoThreshold, "Auto threshold");
    }

protected:
    std::string statusText(Color& c) override
    {
        if (value(kClippedPct) >= 0.01f) {
            c = theme::warm();
            return fmt("REBUILDING  %.2f%% clipped", value(kClippedPct));
        }
        return on(kAutoThreshold) ? fmt("CLIP LEVEL  %.2f dBFS", value(kClipLevel))
                                  : fmt("THRESHOLD  %.2f dBFS", value(kThreshold));
    }

    void idleTick() override { history_.push(value(kClippedPct)); }

    void drawDisplay(const Rect& r) override
    {
        auto scale = [](float pct) { return std::log10(1.0f + pct * 10.0f); }; // 0.1% .. 10%+
        const float top = scale(20.0f);
        for (float v : { 0.1f, 1.0f, 10.0f }) {
            const float y = r.y + r.h - scale(v) / top * r.h;
            beginPath();
            moveTo(r.x, y);
            lineTo(r.x + r.w, y);
            strokeColor(theme::grid());
            strokeWidth(1.0f);
            stroke();
            drawText(*this, r.x + r.w - 6, y - 7, 10.0f, theme::textFaint(), ALIGN_RIGHT | ALIGN_MIDDLE,
                     v < 1.0f ? fmt("%.1f%%", v).c_str() : fmt("%.0f%%", v).c_str());
        }
        History scaled(history_.size());
        for (int i = 0; i < history_.size(); ++i)
            scaled.push(scale(history_.at(i)));
        drawHistory(*this, { r.x, r.y + 30, r.w, r.h - 30 }, scaled, 0.0f, top, theme::warm(), false);

        drawText(*this, r.x + 12, r.y + 14, 11.0f, theme::textDim(), ALIGN_LEFT | ALIGN_MIDDLE, "SAMPLES REBUILT");
        drawText(*this, r.x + 12, r.y + 44, 26.0f, theme::text(), ALIGN_LEFT | ALIGN_MIDDLE,
                 fmt("%.2f%%", value(kClippedPct)).c_str());
        if (value(kOutputGain) >= -0.05f && value(kClippedPct) > 0.01f)
            drawText(*this, r.x + 12, r.y + 68, 11.0f, theme::warm(), ALIGN_LEFT | ALIGN_MIDDLE,
                     "Rebuilt peaks can exceed 0 dBFS: lower the output gain for headroom");
    }

private:
    History history_ { 300 };

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DeClipUI)
};

UI* createUI() { return new DeClipUI(); }

END_NAMESPACE_DISTRHO
