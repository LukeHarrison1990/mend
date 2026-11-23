// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors

#include "mend/ui/ToolUI.hpp"
#include "DeNoiseDisplay.hpp"
#include "DeNoiseParams.hpp"

#include <cmath>
#include <vector>

START_NAMESPACE_DISTRHO

using namespace mend::ui;

namespace {
constexpr float kMinHz = 30.0f, kMaxHz = 20000.0f;
constexpr float kTopDb = 0.0f, kBottomDb = -120.0f;
} // namespace

class DeNoiseUI : public MendToolUI {
public:
    DeNoiseUI()
        : MendToolUI("De-noise", mend::defaultsOf(kDeNoiseParams))
    {
        const auto& p = kDeNoiseParams;
        auto anySound = [this] { return on(kMode); };  // hidden in Voice mode
        auto voiceOnly = [this] { return !on(kMode); }; // hidden in Any-sound mode
        addKnob(kThreshold, "THRESHOLD", p[kThreshold].min, p[kThreshold].max, false, false,
                [](float v) { return fmt("%+.1f dB", v); })
            .isHidden = anySound;
        addKnob(kReduction, "REDUCTION", p[kReduction].min, p[kReduction].max, false, false,
                [](float v) { return fmt("%.1f dB", v); })
            .isHidden = anySound;
        addKnob(kSmoothing, "SMOOTHING", p[kSmoothing].min, p[kSmoothing].max, false, false,
                [](float v) { return fmt("%.0f%%", v); })
            .isHidden = anySound;
        // Voice: one control, in the first slot
        Knob& vr = addKnob(kVoiceReduction, "REDUCTION", p[kVoiceReduction].min, p[kVoiceReduction].max, false, false,
                           [](float v) { return voiceReductionIsFull(v) ? std::string("Full") : fmt("%.0f dB", v); });
        vr.isHidden = voiceOnly;
        vr.bounds = knob(0).bounds;
        // Finishing a learn pass switches to the learned profile.
        addToggle(kLearn, "Learn noise profile", [this](bool learning) {
            if (!learning)
                setParam(kAdaptive, 0.0f);
        });
        addToggle(kAdaptive, "Adaptive");
        addToggle(kNoiseOnly, "Output noise only");
        toggle(0).isHidden = anySound;
        toggle(1).isHidden = anySound;
        addSelector(kMode, "MODE", { 384, 322, 124, 30 }, { "Any sound", "Voice" });
    }

protected:
    std::string statusText(Color& c) override
    {
        if (on(kMode)) {
            c = theme::accent();
            return fmt("VOICE  -%.1f dB", value(kReductionNow));
        }
        if (on(kLearn)) {
            c = theme::warm();
            return "LEARNING  play noise only";
        }
        if (on(kAdaptive)) {
            c = theme::accent();
            return fmt("ADAPTIVE  -%.1f dB", value(kReductionNow));
        }
        if (on(kHasProfile)) {
            c = theme::accent();
            return fmt("PROFILE  -%.1f dB", value(kReductionNow));
        }
        return "NO PROFILE: LEARN OR ADAPTIVE";
    }

    void idleTick() override
    {
        if (display_ == nullptr)
            display_ = denoise_registry::find(getPluginInstancePointer());
        if (display_ != nullptr)
            display_->read(in_, out_, noise_, bins_, fft_, fs_);
    }

    // Per-bin power to dB, with a full-scale sine at about 0 dB.
    float binDb(float power) const
    {
        const double ref = fft_ / 4.0; // Hann-windowed full-scale sine magnitude
        return static_cast<float>(10.0 * std::log10(power + 1e-30) - 20.0 * std::log10(ref));
    }

    // One value per column: the loudest bin under it.
    void columnLevels(const Rect& r, const std::vector<float>& spec, int columns, std::vector<float>& out) const
    {
        out.assign(columns, kBottomDb);
        if (bins_ < 2 || spec.size() < static_cast<size_t>(bins_))
            return;
        const double hzPerBin = fs_ / fft_;
        const double maxHz = std::min<double>(kMaxHz, fs_ * 0.5);
        for (int c = 0; c < columns; ++c) {
            const double f0 = kMinHz * std::pow(maxHz / kMinHz, static_cast<double>(c) / columns);
            const double f1 = kMinHz * std::pow(maxHz / kMinHz, static_cast<double>(c + 1) / columns);
            const double k0 = f0 / hzPerBin, k1 = f1 / hzPerBin;
            const int first = static_cast<int>(std::ceil(k0)), last = static_cast<int>(std::floor(k1));
            float m = 0.0f;
            if (last >= first) {
                // several bins under this column: show the loudest
                for (int k = std::max(first, 1); k <= std::min(last, bins_ - 1); ++k)
                    m = std::max(m, spec[k]);
            } else {
                // column narrower than a bin (low end): interpolate
                const double pos = std::clamp(0.5 * (k0 + k1), 1.0, bins_ - 1.001);
                const int i = static_cast<int>(pos);
                const double t = pos - i;
                m = static_cast<float>(spec[i] * (1.0 - t) + spec[i + 1] * t);
            }
            out[c] = std::clamp(binDb(m), kBottomDb, kTopDb);
        }
        (void)r;
    }

    void drawCurve(const Rect& g, const std::vector<float>& levels, const Color& line, bool filled, float fillAlpha)
    {
        const int n = static_cast<int>(levels.size());
        if (n < 2)
            return;
        auto y = [&](float db) { return g.y + (kTopDb - db) / (kTopDb - kBottomDb) * g.h; };
        const float dx = g.w / static_cast<float>(n - 1);
        if (filled) {
            beginPath();
            moveTo(g.x, g.y + g.h);
            for (int i = 0; i < n; ++i)
                lineTo(g.x + i * dx, y(levels[i]));
            lineTo(g.x + g.w, g.y + g.h);
            closePath();
            Color top = line, bottom = line;
            top.alpha = fillAlpha;
            bottom.alpha = 0.0f;
            fillPaint(linearGradient(0, g.y, 0, g.y + g.h, top, bottom));
            fill();
        }
        beginPath();
        moveTo(g.x, y(levels[0]));
        for (int i = 1; i < n; ++i)
            lineTo(g.x + i * dx, y(levels[i]));
        strokeColor(line);
        strokeWidth(1.5f);
        stroke();
    }

    void drawDisplay(const Rect& r) override
    {
        const Rect g { r.x, r.y + 24, r.w, r.h - 40 };
        const double maxHz = std::min<double>(kMaxHz, fs_ > 0 ? fs_ * 0.5 : kMaxHz);
        auto fx = [&](double f) {
            return g.x + static_cast<float>(std::log(f / kMinHz) / std::log(maxHz / kMinHz)) * g.w;
        };
        for (float f : { 100.f, 1000.f, 10000.f }) {
            beginPath();
            moveTo(fx(f), r.y);
            lineTo(fx(f), r.y + r.h);
            strokeColor(theme::grid());
            strokeWidth(1.0f);
            stroke();
            drawText(*this, fx(f) + 4, r.y + r.h - 9, 10.0f, theme::textFaint(), ALIGN_LEFT | ALIGN_MIDDLE,
                     f >= 1000 ? fmt("%.0fk", f / 1000).c_str() : fmt("%.0f", f).c_str());
        }
        for (float db : { -20.f, -50.f, -80.f, -110.f }) {
            const float yy = g.y + (kTopDb - db) / (kTopDb - kBottomDb) * g.h;
            beginPath();
            moveTo(r.x, yy);
            lineTo(r.x + r.w, yy);
            strokeColor(theme::grid());
            stroke();
            drawText(*this, r.x + r.w - 6, yy - 7, 10.0f, theme::textFaint(), ALIGN_RIGHT | ALIGN_MIDDLE,
                     fmt("%.0f dB", db).c_str());
        }

        // Columns at the real pixel density, so the curves stay sharp.
        const int columns = std::max(64, static_cast<int>(g.w * layoutScale() / 2.0));
        columnLevels(g, in_, columns, colIn_);
        columnLevels(g, out_, columns, colOut_);
        columnLevels(g, noise_, columns, colNoise_);

        drawCurve(g, colIn_, Color(140, 147, 160, 0.55f), true, 0.10f);
        drawCurve(g, colOut_, theme::accent(), true, 0.18f);
        bool anyNoise = false;
        for (float v : colNoise_)
            anyNoise |= v > kBottomDb + 0.5f;
        if (anyNoise)
            drawCurve(g, colNoise_, theme::warm(), false, 0.0f);

        // legend
        float lx = r.x + 12;
        const float ly = r.y + 14;
        auto legend = [&](const Color& c, const char* label) {
            beginPath();
            roundedRect(lx, ly - 1.5f, 14, 3, 1.5f);
            fillColor(c);
            fill();
            drawText(*this, lx + 20, ly, 11.0f, theme::textDim(), ALIGN_LEFT | ALIGN_MIDDLE, label);
            lx += 20 + 8.0f * static_cast<float>(std::string(label).size()) + 18;
        };
        legend(Color(140, 147, 160), "INPUT");
        legend(theme::accent(), "OUTPUT");
        if (!on(kMode))
            legend(theme::warm(), "NOISE PROFILE");
    }

private:
    DeNoiseDisplay* display_ = nullptr;
    std::vector<float> in_, out_, noise_, colIn_, colOut_, colNoise_;
    int bins_ = 0, fft_ = 2048;
    double fs_ = 48000.0;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DeNoiseUI)
};

UI* createUI() { return new DeNoiseUI(); }

END_NAMESPACE_DISTRHO
