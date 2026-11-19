// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors

#include "DeHumParams.hpp"
#include "mend/DeHum.hpp"
#include "mend/ui/ToolUI.hpp"

#include <array>
#include <cmath>
#include <vector>

START_NAMESPACE_DISTRHO

using namespace mend::ui;

namespace {
constexpr float kMinHz = 20.0f, kMaxHz = 2000.0f;
constexpr float kMinDb = -66.0f, kMaxDb = 12.0f;

std::vector<float> defaults()
{
    std::vector<float> d(kParamCount);
    for (unsigned i = 0; i < kParamCount; ++i)
        d[i] = kDeHumRanges[i].def;
    return d;
}
} // namespace

class DeHumUI : public MendToolUI {
public:
    DeHumUI()
        : MendToolUI("De-hum", defaults())
    {
        auto r = [](unsigned p) { return kDeHumRanges[p]; };
        Knob& f = addKnob(kParamFrequency, "FREQUENCY", r(kParamFrequency).min, r(kParamFrequency).max, false, false,
                          [](float v) { return fmt("%.2f Hz", v); });
        // While tracking, show the frequency in use; grabbing the knob
        // switches to manual from that frequency.
        f.isAutomatic = [this] { return on(kParamAuto); };
        f.displayValue = [this] { return on(kParamAuto) ? value(kParamDetectedFreq) : value(kParamFrequency); };
        f.onGrab = [this] {
            if (on(kParamAuto)) {
                setParam(kParamAuto, 0.0f);
                setParam(kParamFrequency, value(kParamDetectedFreq));
            }
        };
        addKnob(kParamHarmonics, "HARMONICS", r(kParamHarmonics).min, r(kParamHarmonics).max, false, true,
                [](float v) { return fmt("%.0f", v); });
        addKnob(kParamQ, "Q", r(kParamQ).min, r(kParamQ).max, true, false, [](float v) { return fmt("%.0f", v); });
        addKnob(kParamReduction, "REDUCTION", r(kParamReduction).min, r(kParamReduction).max, false, false,
                [](float v) { return fmt("%.1f dB", v); });
        addToggle(kParamAuto, "Auto-detect hum");
        addToggle(kParamHumOnly, "Output hum only");
        setMeter(kParamRemovedLevel, "REMOVED", -90.0f, 0.0f);
    }

protected:
    std::string statusText(Color& c) override
    {
        if (!on(kParamAuto))
            return fmt("MANUAL  %.2f Hz", value(kParamFrequency));
        if (value(kParamConfidence) >= 0.35f) {
            c = theme::warm();
            return fmt("TRACKING  %.2f Hz", value(kParamDetectedFreq));
        }
        return "LISTENING FOR HUM";
    }

    float freqToX(const Rect& g, float f) const
    {
        return g.x + std::log(f / kMinHz) / std::log(kMaxHz / kMinHz) * g.w;
    }
    float dbToY(const Rect& g, float db) const
    {
        return g.y + std::clamp((db - kMaxDb) / (kMinDb - kMaxDb), 0.0f, 1.0f) * g.h;
    }

    void drawDisplay(const Rect& g) override
    {
        for (float f : { 50.f, 100.f, 200.f, 500.f, 1000.f }) {
            const float x = freqToX(g, f);
            beginPath();
            moveTo(x, g.y);
            lineTo(x, g.y + g.h);
            strokeColor(theme::grid());
            strokeWidth(1.0f);
            stroke();
            drawText(*this, x + 4, g.y + g.h - 10, 10.0f, theme::textFaint(), ALIGN_LEFT | ALIGN_MIDDLE,
                     f >= 1000 ? "1k" : fmt("%.0f", f).c_str());
        }
        for (float db : { 0.f, -20.f, -40.f, -60.f }) {
            const float y = dbToY(g, db);
            beginPath();
            moveTo(g.x, y);
            lineTo(g.x + g.w, y);
            strokeColor(db == 0.f ? theme::gridStrong() : theme::grid());
            stroke();
            drawText(*this, g.x + g.w - 6, y - 7, 10.0f, theme::textFaint(), ALIGN_RIGHT | ALIGN_MIDDLE,
                     fmt("%.0f dB", db).c_str());
        }

        // Filter response, computed with the DSP's own design function.
        mend::DeHum::Settings s;
        s.harmonics = static_cast<int>(std::lround(value(kParamHarmonics)));
        s.q = value(kParamQ);
        s.reductionDb = value(kParamReduction);
        const double f0 = on(kParamAuto) ? value(kParamDetectedFreq) : value(kParamFrequency);
        double fs = getSampleRate();
        if (fs <= 0.0)
            fs = 48000.0;
        std::array<mend::BiquadCoeffs, mend::DeHum::kMaxHarmonics> cs;
        // Flat (and no markers) while auto-detect has not found any hum.
        const int n = on(kParamActive) || !on(kParamAuto) ? mend::DeHum::designCascade(fs, f0, s, cs) : 0;

        const int steps = static_cast<int>(g.w * layoutScale()); // one point per real pixel
        const float dx = g.w / static_cast<float>(steps);
        std::vector<float> ys(steps + 1);
        for (int i = 0; i <= steps; ++i) {
            const double f = kMinHz * std::pow(kMaxHz / kMinHz, static_cast<double>(i) / steps);
            // narrow notches fall between pixels: take the deepest sub-sample
            double mag = 1e9;
            for (int sub = 0; sub < 8; ++sub) {
                const double fsub = f * std::pow(kMaxHz / kMinHz, (sub / 8.0 - 0.5) / steps);
                double m = 1.0;
                for (int k = 0; k < n; ++k)
                    m *= std::abs(cs[k].response(fs, fsub));
                mag = std::min(mag, m);
            }
            ys[i] = dbToY(g, static_cast<float>(mend::gainToDb(mag)));
        }

        for (int h = 1; h <= n; ++h) {
            const float x = freqToX(g, static_cast<float>(f0 * h));
            beginPath();
            moveTo(x, g.y);
            lineTo(x, g.y + g.h);
            strokeColor(theme::warmSoft());
            strokeWidth(1.0f);
            stroke();
        }

        beginPath();
        moveTo(g.x, g.y + g.h);
        for (int i = 0; i <= steps; ++i)
            lineTo(g.x + i * dx, ys[i]);
        lineTo(g.x + g.w, g.y + g.h);
        closePath();
        fillPaint(linearGradient(0, g.y, 0, g.y + g.h, Color(64, 214, 196, 0.22f), Color(64, 214, 196, 0.0f)));
        fill();

        beginPath();
        moveTo(g.x, ys[0]);
        for (int i = 1; i <= steps; ++i)
            lineTo(g.x + i * dx, ys[i]);
        strokeColor(on(kParamHumOnly) ? theme::warm() : theme::accent());
        strokeWidth(2.0f);
        stroke();

        drawText(*this, g.x + 12, g.y + 14, 11.0f, theme::textDim(), ALIGN_LEFT | ALIGN_MIDDLE, "FILTER RESPONSE");
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DeHumUI)
};

UI* createUI() { return new DeHumUI(); }

END_NAMESPACE_DISTRHO
