// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors

#include "mend/ui/ToolUI.hpp"
#include "BreathControlParams.hpp"

#include <algorithm>
#include <cmath>

START_NAMESPACE_DISTRHO

using namespace mend::ui;

class BreathControlUI : public MendToolUI {
public:
    BreathControlUI()
        : MendToolUI("Breath Control", mend::defaultsOf(kBreathControlParams))
    {
        const auto& p = kBreathControlParams;
        addKnob(kSensitivity, "SENSITIVITY", p[kSensitivity].min, p[kSensitivity].max, false, false,
                [](float v) { return fmt("%.0f", v); });
        addKnob(kReductionAmount, "REDUCTION", p[kReductionAmount].min, p[kReductionAmount].max, false, false,
                [](float v) { return fmt("%.0f dB", v); });
    }

protected:
    std::string statusText(Color& c) override
    {
        if (value(kReduction) >= 1.0f) {
            c = theme::warm();
            return fmt("REDUCING  %.1f dB", value(kReduction));
        }
        return "LISTENING FOR BREATHS";
    }

    void idleTick() override { history_.push(value(kReduction)); }

    void drawDisplay(const Rect& r) override
    {
        const Rect g { r.x, r.y + 30, r.w, r.h - 40 };
        const float range = 30.0f;
        for (float db : { 0.f, 10.f, 20.f, 30.f }) {
            const float y = g.y + db / range * g.h;
            beginPath();
            moveTo(r.x, y);
            lineTo(r.x + r.w, y);
            strokeColor(db == 0.f ? theme::gridStrong() : theme::grid());
            strokeWidth(1.0f);
            stroke();
            drawText(*this, r.x + r.w - 6, y + (db == 0.f ? 8 : -7), 10.0f, theme::textFaint(), ALIGN_RIGHT | ALIGN_MIDDLE,
                     fmt("-%.0f dB", db).c_str());
        }
        drawHistory(*this, g, history_, 0.0f, range, theme::warm(), true);
        drawText(*this, r.x + 12, r.y + 14, 11.0f, theme::textDim(), ALIGN_LEFT | ALIGN_MIDDLE, "BREATH GAIN REDUCTION");
    }

private:
    History history_ { 300 };

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BreathControlUI)
};

UI* createUI() { return new BreathControlUI(); }

END_NAMESPACE_DISTRHO
