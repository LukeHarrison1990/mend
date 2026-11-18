// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors

#include "mend/ui/ToolUI.hpp"
#include "DeEssParams.hpp"

#include <algorithm>
#include <cmath>

START_NAMESPACE_DISTRHO

using namespace mend::ui;

class DeEssUI : public MendToolUI {
public:
    DeEssUI()
        : MendToolUI("De-ess", mend::defaultsOf(kDeEssParams))
    {
        const auto& p = kDeEssParams;
        addKnob(kThreshold, "THRESHOLD", p[kThreshold].min, p[kThreshold].max, false, false,
                [](float v) { return fmt("%.1f dB", v); });
        addKnob(kFrequency, "FREQUENCY", p[kFrequency].min, p[kFrequency].max, true, false,
                [](float v) { return fmt("%.0f Hz", v); });
        addKnob(kMaxReduction, "MAX REDUCTION", p[kMaxReduction].min, p[kMaxReduction].max, false, false,
                [](float v) { return fmt("%.0f dB", v); });
        addToggle(kWideband, "Wideband");
    }

protected:
    std::string statusText(Color& c) override
    {
        if (value(kReduction) >= 1.0f) {
            c = theme::warm();
            return fmt("REDUCING  %.1f dB", value(kReduction));
        }
        return "LISTENING FOR SIBILANCE";
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
        drawText(*this, r.x + 12, r.y + 14, 11.0f, theme::textDim(), ALIGN_LEFT | ALIGN_MIDDLE, "SIBILANCE GAIN REDUCTION");
    }

private:
    History history_ { 300 };

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DeEssUI)
};

UI* createUI() { return new DeEssUI(); }

END_NAMESPACE_DISTRHO
