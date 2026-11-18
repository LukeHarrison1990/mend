// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors

#include "mend/ui/ToolUI.hpp"
#include "CentreExtractParams.hpp"

#include <algorithm>
#include <cmath>

START_NAMESPACE_DISTRHO

using namespace mend::ui;

class CentreExtractUI : public MendToolUI {
public:
    CentreExtractUI()
        : MendToolUI("Centre Extract", mend::defaultsOf(kCentreExtractParams))
    {
        const auto& p = kCentreExtractParams;
        addKnob(kStrength, "STRENGTH", p[kStrength].min, p[kStrength].max, false, false,
                [](float v) { return fmt("%.0f", v); });
        addKnob(kAmount, "AMOUNT", p[kAmount].min, p[kAmount].max, false, false,
                [](float v) { return fmt("%.0f%%", v); });
        addToggle(kRemoveCentre, "Remove centre");
    }

protected:
    std::string statusText(Color& c) override
    {
        c = theme::accent();
        return on(kRemoveCentre) ? "REMOVING THE CENTRE" : "KEEPING THE CENTRE";
    }

    void idleTick() override { in_.push(-value(kInputLevel));
        out_.push(-value(kOutputLevel)); }

    void drawDisplay(const Rect& r) override
    {
        const Rect g { r.x, r.y + 30, r.w, r.h - 40 };
        const float range = 72.0f; // 0 dBFS at the top
        for (float db : { 0.f, 24.f, 48.f, 72.f }) {
            const float y = g.y + db / range * g.h;
            beginPath();
            moveTo(r.x, y);
            lineTo(r.x + r.w, y);
            strokeColor(theme::grid());
            strokeWidth(1.0f);
            stroke();
            drawText(*this, r.x + r.w - 6, y + (db == 0.f ? 8 : -7), 10.0f, theme::textFaint(), ALIGN_RIGHT | ALIGN_MIDDLE,
                     fmt("-%.0f dB", db).c_str());
        }
        // input as a faint fill, output as the line on top
        drawHistory(*this, g, in_, 0.0f, range, mend::ui::Color(140, 147, 160), false);
        drawHistory(*this, g, out_, 0.0f, range, theme::accent(), false);
        drawText(*this, r.x + 12, r.y + 14, 11.0f, theme::textDim(), ALIGN_LEFT | ALIGN_MIDDLE, "LEVEL IN AND OUT");
        drawText(*this, r.x + r.w - 160, r.y + 14, 10.5f, mend::ui::Color(140, 147, 160), ALIGN_LEFT | ALIGN_MIDDLE, "INPUT");
        drawText(*this, r.x + r.w - 100, r.y + 14, 10.5f, theme::accent(), ALIGN_LEFT | ALIGN_MIDDLE, "OUTPUT");
    }

private:
    History in_ { 300 }, out_ { 300 };

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CentreExtractUI)
};

UI* createUI() { return new CentreExtractUI(); }

END_NAMESPACE_DISTRHO
