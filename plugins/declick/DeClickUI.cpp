// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors

#include "mend/ui/ToolUI.hpp"
#include "DeClickParams.hpp"

#include <cmath>

START_NAMESPACE_DISTRHO

using namespace mend::ui;

class DeClickUI : public MendToolUI {
public:
    DeClickUI()
        : MendToolUI("De-click", mend::defaultsOf(kDeClickParams))
    {
        const auto& p = kDeClickParams;
        addKnob(kSensitivity, "SENSITIVITY", p[kSensitivity].min, p[kSensitivity].max, false, false,
                [](float v) { return fmt("%.0f", v); });
        addKnob(kClickWidth, "CLICK WIDTH", p[kClickWidth].min, p[kClickWidth].max, true, false,
                [](float v) { return fmt("%.2f ms", v); });
        addToggle(kClicksOnly, "Output clicks only");
    }

protected:
    std::string statusText(Color& c) override
    {
        const float rate = value(kClickRate);
        if (rate >= 0.5f) {
            c = theme::warm();
            return fmt("REPAIRING  %.0f clicks/s", rate);
        }
        return "LISTENING FOR CLICKS";
    }

    void idleTick() override { history_.push(value(kClickRate)); }

    void drawDisplay(const Rect& r) override
    {
        // log-ish scale so a few clicks and heavy crackle both read well
        auto scale = [](float v) { return std::log10(1.0f + v); };
        const float top = scale(500.0f);
        for (float v : { 1.f, 10.f, 100.f }) {
            const float y = r.y + r.h - scale(v) / top * r.h;
            beginPath();
            moveTo(r.x, y);
            lineTo(r.x + r.w, y);
            strokeColor(theme::grid());
            strokeWidth(1.0f);
            stroke();
            drawText(*this, r.x + r.w - 6, y - 7, 10.0f, theme::textFaint(), ALIGN_RIGHT | ALIGN_MIDDLE,
                     fmt("%.0f/s", v).c_str());
        }

        History scaled(history_.size());
        for (int i = 0; i < history_.size(); ++i)
            scaled.push(scale(history_.at(i)));
        drawHistory(*this, { r.x, r.y + 30, r.w, r.h - 30 }, scaled, 0.0f, top, theme::warm(), false);

        drawText(*this, r.x + 12, r.y + 14, 11.0f, theme::textDim(), ALIGN_LEFT | ALIGN_MIDDLE, "CLICKS REPAIRED");
        drawText(*this, r.x + 12, r.y + 44, 26.0f, theme::text(), ALIGN_LEFT | ALIGN_MIDDLE,
                 fmt("%.0f", value(kClickRate)).c_str());
        drawText(*this, r.x + 12, r.y + 68, 11.0f, theme::textDim(), ALIGN_LEFT | ALIGN_MIDDLE, "per second");
    }

private:
    History history_ { 300 };

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DeClickUI)
};

UI* createUI() { return new DeClickUI(); }

END_NAMESPACE_DISTRHO
