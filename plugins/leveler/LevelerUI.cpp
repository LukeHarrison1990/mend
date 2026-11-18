// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors

#include "mend/ui/ToolUI.hpp"
#include "LevelerParams.hpp"

#include <algorithm>
#include <cmath>

START_NAMESPACE_DISTRHO

using namespace mend::ui;

class LevelerUI : public MendToolUI {
public:
    LevelerUI()
        : MendToolUI("Leveler", mend::defaultsOf(kLevelerParams))
    {
        const auto& p = kLevelerParams;
        addKnob(kTarget, "TARGET", p[kTarget].min, p[kTarget].max, false, false,
                [](float v) { return fmt("%.1f LUFS", v); });
        addKnob(kMaxBoost, "MAX BOOST", p[kMaxBoost].min, p[kMaxBoost].max, false, false,
                [](float v) { return fmt("%.0f dB", v); });
        addKnob(kMaxCut, "MAX CUT", p[kMaxCut].min, p[kMaxCut].max, false, false,
                [](float v) { return fmt("%.0f dB", v); });
        addKnob(kSpeed, "SPEED", p[kSpeed].min, p[kSpeed].max, true, false,
                [](float v) { return fmt("%.1f s", v); });
        addKnob(kGate, "GATE", p[kGate].min, p[kGate].max, false, false,
                [](float v) { return fmt("%.0f dB", v); });
    }

protected:
    std::string statusText(Color& c) override
    {
        c = theme::accent();
        return fmt("GAIN  %+.1f dB", value(kGain));
    }

    void idleTick() override { history_.push(value(kGain)); }

    void drawDisplay(const Rect& r) override
    {
        const Rect g { r.x, r.y + 30, r.w, r.h - 40 };
        const float range = 24.0f;
        for (float db : { 24.f, 12.f, 0.f, -12.f, -24.f }) {
            const float y = g.y + (range - db) / (2 * range) * g.h;
            beginPath();
            moveTo(r.x, y);
            lineTo(r.x + r.w, y);
            strokeColor(db == 0.f ? theme::gridStrong() : theme::grid());
            strokeWidth(1.0f);
            stroke();
            drawText(*this, r.x + r.w - 6, y - 7, 10.0f, theme::textFaint(), ALIGN_RIGHT | ALIGN_MIDDLE, fmt("%+.0f dB", db).c_str());
        }
        // gain line, centred on 0 dB
        const int n = history_.size();
        beginPath();
        for (int i = 0; i < n; ++i) {
            const float x = g.x + g.w * i / (n - 1);
            const float y = g.y + (range - std::clamp(history_.at(i), -range, range)) / (2 * range) * g.h;
            if (i == 0)
                moveTo(x, y);
            else
                lineTo(x, y);
        }
        strokeColor(theme::accent());
        strokeWidth(2.0f);
        stroke();
        drawText(*this, r.x + 12, r.y + 14, 11.0f, theme::textDim(), ALIGN_LEFT | ALIGN_MIDDLE, "LEVELLING GAIN");
    }

private:
    History history_ { 300 };

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LevelerUI)
};

UI* createUI() { return new LevelerUI(); }

END_NAMESPACE_DISTRHO
