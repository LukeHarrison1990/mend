// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors

#include "mend/ui/ToolUI.hpp"
#include "EqParams.hpp"
#include "mend/Eq.hpp"

#include <algorithm>
#include <cmath>

START_NAMESPACE_DISTRHO

using namespace mend::ui;

class EqUI : public MendToolUI {
public:
    EqUI()
        : MendToolUI("EQ", mend::defaultsOf(kEqParams))
    {
        const auto& p = kEqParams;
        addKnob(kHighPass, "HIGH-PASS", p[kHighPass].min, p[kHighPass].max, true, false,
                [](float v) { return v <= 20.01f ? std::string("Off") : fmt("%.0f Hz", v); });
        addKnob(kLowShelf, "LOW SHELF", p[kLowShelf].min, p[kLowShelf].max, true, false,
                [](float v) { return fmt("%.0f Hz", v); });
        addKnob(kLowShelfGain, "SHELF GAIN", p[kLowShelfGain].min, p[kLowShelfGain].max, false, false,
                [](float v) { return fmt("%+.1f dB", v); });
        addKnob(kBand1, "BAND 1", p[kBand1].min, p[kBand1].max, true, false,
                [](float v) { return fmt("%.0f Hz", v); });
        addKnob(kGain1, "GAIN 1", p[kGain1].min, p[kGain1].max, false, false,
                [](float v) { return fmt("%+.1f dB", v); });
        addKnob(kQ1, "Q 1", p[kQ1].min, p[kQ1].max, true, false,
                [](float v) { return fmt("%.2f", v); });
        addKnob(kBand2, "BAND 2", p[kBand2].min, p[kBand2].max, true, false,
                [](float v) { return fmt("%.0f Hz", v); });
        addKnob(kGain2, "GAIN 2", p[kGain2].min, p[kGain2].max, false, false,
                [](float v) { return fmt("%+.1f dB", v); });
        addKnob(kQ2, "Q 2", p[kQ2].min, p[kQ2].max, true, false,
                [](float v) { return fmt("%.2f", v); });
        addKnob(kBand3, "BAND 3", p[kBand3].min, p[kBand3].max, true, false,
                [](float v) { return fmt("%.0f Hz", v); });
        addKnob(kGain3, "GAIN 3", p[kGain3].min, p[kGain3].max, false, false,
                [](float v) { return fmt("%+.1f dB", v); });
        addKnob(kQ3, "Q 3", p[kQ3].min, p[kQ3].max, true, false,
                [](float v) { return fmt("%.2f", v); });
        addKnob(kHighShelf, "HIGH SHELF", p[kHighShelf].min, p[kHighShelf].max, true, false,
                [](float v) { return fmt("%.0f Hz", v); });
        addKnob(kHighShelfGain, "SHELF GAIN", p[kHighShelfGain].min, p[kHighShelfGain].max, false, false,
                [](float v) { return fmt("%+.1f dB", v); });
        addKnob(kLowPass, "LOW-PASS", p[kLowPass].min, p[kLowPass].max, true, false,
                [](float v) { return v >= 19999.0f ? std::string("Off") : fmt("%.0f Hz", v); });
        // one column per band: high/low-pass, low shelf, three peaks, high shelf
        const int cells[15][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 2, 0 }, { 2, 1 }, { 2, 2 }, { 3, 0 }, { 3, 1 },
                                   { 3, 2 }, { 4, 0 }, { 4, 1 }, { 4, 2 }, { 5, 0 }, { 5, 1 }, { 0, 1 } };
        for (size_t i = 0; i < 15; ++i)
            placeKnob(i, cells[i][0], cells[i][1]);
    }

protected:
    std::string statusText(Color& c) override
    {
        c = theme::accent();
        return "PARAMETRIC EQ";
    }

    void idleTick() override {  }

    void drawDisplay(const Rect& r) override
    {
        // the response computed from the knob values
        mend::ParametricEq eq;
        eq.prepare(sampleRateForDisplay() > 0 ? sampleRateForDisplay() : 48000.0, 1);
        mend::ParametricEq::Settings s;
        s.highPassHz = value(kHighPass);
        s.lowShelfHz = value(kLowShelf);
        s.lowShelfDb = value(kLowShelfGain);
        s.peakHz = { value(kBand1), value(kBand2), value(kBand3) };
        s.peakDb = { value(kGain1), value(kGain2), value(kGain3) };
        s.peakQ = { value(kQ1), value(kQ2), value(kQ3) };
        s.highShelfHz = value(kHighShelf);
        s.highShelfDb = value(kHighShelfGain);
        s.lowPassHz = value(kLowPass);
        eq.setSettings(s);
        const Rect g { r.x + 8, r.y + 28, r.w - 16, r.h - 36 };
        const float range = 24.0f;
        const auto xOf = [&](double hz) { return g.x + static_cast<float>(std::log(hz / 20.0) / std::log(1000.0)) * g.w; };
        for (double hz : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 }) {
            beginPath();
            moveTo(xOf(hz), g.y);
            lineTo(xOf(hz), g.y + g.h);
            strokeColor(theme::grid());
            strokeWidth(1.0f);
            stroke();
            drawText(*this, xOf(hz) + 3, g.y + g.h - 7, 9.5f, theme::textFaint(), ALIGN_LEFT | ALIGN_MIDDLE,
                     hz >= 1000 ? fmt("%.0fk", static_cast<float>(hz / 1000)) .c_str() : fmt("%.0f", static_cast<float>(hz)).c_str());
        }
        for (float db : { 12.f, 0.f, -12.f }) {
            const float y = g.y + (range - db) / (2 * range) * g.h;
            beginPath();
            moveTo(g.x, y);
            lineTo(g.x + g.w, y);
            strokeColor(db == 0.f ? theme::gridStrong() : theme::grid());
            stroke();
            drawText(*this, g.x + g.w - 4, y - 7, 9.5f, theme::textFaint(), ALIGN_RIGHT | ALIGN_MIDDLE, fmt("%+.0f dB", db).c_str());
        }
        beginPath();
        for (int i = 0; i <= static_cast<int>(g.w); i += 2) {
            const double hz = 20.0 * std::pow(1000.0, i / g.w);
            const float d = std::clamp(static_cast<float>(eq.responseDb(hz)), -range, range);
            const float y = g.y + (range - d) / (2 * range) * g.h;
            if (i == 0)
                moveTo(g.x + i, y);
            else
                lineTo(g.x + i, y);
        }
        strokeColor(theme::accent());
        strokeWidth(2.2f);
        stroke();
        drawText(*this, r.x + 12, r.y + 14, 11.0f, theme::textDim(), ALIGN_LEFT | ALIGN_MIDDLE, "FREQUENCY RESPONSE");
    }

private:


    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EqUI)
};

UI* createUI() { return new EqUI(); }

END_NAMESPACE_DISTRHO
