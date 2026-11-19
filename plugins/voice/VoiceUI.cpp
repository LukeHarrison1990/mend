// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors

#include "mend/ui/ToolUI.hpp"
#include "VoiceParams.hpp"

#include <algorithm>
#include <cmath>

START_NAMESPACE_DISTRHO

using namespace mend::ui;

class VoiceUI : public MendToolUI {
public:
    VoiceUI()
        : MendToolUI("Voice", mend::defaultsOf(kVoiceParams))
    {
        std::vector<std::string> mics, groups, characters;
        for (int i = 0; i < mend::VoiceClean::numMics(); ++i) {
            mics.push_back(mend::VoiceClean::mic(i).name);
            groups.push_back(mend::VoiceClean::mic(i).group);
        }
        for (int i = 0; i < mend::VoiceClean::numCharacters(); ++i)
            characters.push_back(mend::VoiceClean::character(i).name);

        // Choosing a microphone also sets the three controls to that
        // profile's starting positions.
        addSelector(kMic, "MICROPHONE", { 36, 322, 214, 30 }, mics, groups, [this](int i) {
            const auto& m = mend::VoiceClean::mic(i);
            setParam(kClarity, m.clarity);
            setParam(kSmooth, m.smooth);
            setParam(kDeMud, m.deMud);
        });
        addSelector(kCharacter, "CHARACTER", { 36, 378, 214, 30 }, characters);

        const float x0 = 270, y = 304, h = 106;
        auto pct = [](float v) { return fmt("%.0f", v); };
        addKnob(kClarity, "CLARITY", 0, 100, false, false, pct).bounds = { x0, y, 112, h };
        addKnob(kSmooth, "SMOOTH", 0, 100, false, false, pct).bounds = { x0 + 112, y, 112, h };
        addKnob(kDeMud, "DE-MUD", 0, 100, false, false, pct).bounds = { x0 + 224, y, 112, h };
        addKnob(kOutput, "OUTPUT", -12, 12, false, false, [](float v) { return fmt("%+.1f dB", v); }).bounds
            = { x0 + 360, y, 112, h };

        eq_.prepare(48000.0, 1);
        pushEq();
    }

protected:
    // Read the processor's stages (fast rise, slower fall for the meters).
    void idleTick() override
    {
        const auto* r = mend::voiceReadout(getPluginInstancePointer());
        if (r == nullptr)
            return;
        const float now[kStages] = { r->levelDb.load(), r->deMudDb.load(),   r->clarityDb.load(), r->resonanceDb.load(),
                                     r->compressDb.load(), r->deEssDb.load(), r->limitDb.load() };
        for (int i = 0; i < kStages; ++i)
            stage_[i] = now[i] > stage_[i] || i == 0 ? now[i] : stage_[i] + 0.2f * (now[i] - stage_[i]);
        peakDb_ = r->outputPeakDb.load();
    }

    std::string statusText(Color& c) override
    {
        if (peakDb_ <= -59.0f)
            return "WAITING FOR A VOICE";
        c = theme::accent();
        return std::string("POLISHING  ") + mend::VoiceClean::character(static_cast<int>(value(kCharacter))).name;
    }

    void drawDisplay(const Rect& r) override
    {
        pushEq();
        drawCurve({ r.x + 12, r.y + 30, r.w * 0.52f - 18, r.h - 42 });
        drawStages({ r.x + r.w * 0.52f + 8, r.y + 12, r.w * 0.48f - 20, r.h - 20 });
        drawText(*this, r.x + 12, r.y + 14, 11.0f, theme::textDim(), ALIGN_LEFT | ALIGN_MIDDLE,
                 "MICROPHONE AND CHARACTER EQ");
    }

private:
    void pushEq()
    {
        const auto& cur = eq_.settings();
        if (cur.mic == static_cast<int>(std::lround(value(kMic))) && cur.character == static_cast<int>(std::lround(value(kCharacter)))
            && cur.deMud == value(kDeMud))
            return;
        mend::VoiceClean::Settings s;
        s.mic = static_cast<int>(std::lround(value(kMic)));
        s.character = static_cast<int>(std::lround(value(kCharacter)));
        s.deMud = value(kDeMud);
        eq_.setSettings(s);
    }

    void drawCurve(const Rect& g)
    {
        const float lo = std::log(40.0f), hi = std::log(16000.0f), range = 12.0f;
        auto xOf = [&](float hz) { return g.x + (std::log(hz) - lo) / (hi - lo) * g.w; };
        auto yOf = [&](float db) { return g.y + g.h * 0.5f - db / range * g.h * 0.5f; };
        for (float hz : { 100.f, 1000.f, 10000.f }) {
            beginPath();
            moveTo(xOf(hz), g.y);
            lineTo(xOf(hz), g.y + g.h);
            strokeColor(theme::grid());
            strokeWidth(1.0f);
            stroke();
            drawText(*this, xOf(hz) + 3, g.y + g.h - 7, 10.0f, theme::textFaint(), ALIGN_LEFT | ALIGN_MIDDLE,
                     hz >= 1000.f ? fmt("%.0fk", hz / 1000.f).c_str() : fmt("%.0f", hz).c_str());
        }
        for (float db : { -6.f, 0.f, 6.f }) {
            beginPath();
            moveTo(g.x, yOf(db));
            lineTo(g.x + g.w, yOf(db));
            strokeColor(db == 0.f ? theme::gridStrong() : theme::grid());
            strokeWidth(1.0f);
            stroke();
        }
        // The live de-mud cut on top of the fixed EQ.
        const double mud = std::max(0.0, stage_[1] - 2.5 * value(kDeMud) / 100.0);
        const auto mudC = mend::BiquadCoeffs::peaking(48000.0, 330.0, 0.9, mend::dbToGain(-mud));
        beginPath();
        for (int i = 0; i <= 160; ++i) {
            const float hz = std::exp(lo + (hi - lo) * i / 160.0f);
            double db = eq_.staticResponseDb(hz) + mend::gainToDb(magnitude(mudC, hz));
            const float y = yOf(static_cast<float>(std::clamp(db, -12.0, 12.0)));
            i == 0 ? moveTo(xOf(hz), y) : lineTo(xOf(hz), y);
        }
        strokeColor(theme::accent());
        strokeWidth(2.0f);
        stroke();
    }

    static double magnitude(const mend::BiquadCoeffs& c, double hz)
    {
        const double w = 2.0 * mend::kPi * hz / 48000.0;
        const double cr = std::cos(w), ci = -std::sin(w), c2r = std::cos(2 * w), c2i = -std::sin(2 * w);
        const double nr = c.b0 + c.b1 * cr + c.b2 * c2r, ni = c.b1 * ci + c.b2 * c2i;
        const double dr = 1.0 + c.a1 * cr + c.a2 * c2r, di = c.a1 * ci + c.a2 * c2i;
        return std::sqrt((nr * nr + ni * ni) / (dr * dr + di * di));
    }

    // One bar per stage, moving with what it is doing right now.
    void drawStages(const Rect& r)
    {
        struct Row {
            const char* name;
            float max;
            bool bipolar;
        };
        const Row rows[kStages] = {
            { "LEVELLING", 12.0f, true }, { "DE-MUD", 12.0f, false },  { "CLARITY", 6.0f, false },
            { "RESONANCES", 8.0f, false }, { "DYNAMICS", 15.0f, false }, { "DE-ESS", 10.0f, false },
            { "LIMITER", 10.0f, false },
        };
        const float rowH = r.h / 7.0f, labelW = 92.0f, valueW = 54.0f;
        for (int i = 0; i < 7; ++i) {
            const Row& row = rows[i];
            const float y = r.y + rowH * i + rowH * 0.5f;
            const float v = stage_[i];
            drawText(*this, r.x, y, 10.5f, theme::textDim(), ALIGN_LEFT | ALIGN_MIDDLE, row.name);
            const Rect bar { r.x + labelW, y - 4, r.w - labelW - valueW, 8 };
            beginPath();
            roundedRect(bar.x, bar.y, bar.w, bar.h, 4);
            fillColor(theme::well());
            fill();
            float a = 0.0f, b = 0.0f;
            if (row.bipolar) {
                const float mid = bar.x + bar.w * 0.5f;
                const float e = mid + std::clamp(v / row.max, -1.0f, 1.0f) * bar.w * 0.5f;
                a = std::min(mid, e);
                b = std::max(mid, e);
            } else {
                a = bar.x;
                b = bar.x + std::clamp(v / row.max, 0.0f, 1.0f) * bar.w;
            }
            if (b - a > 0.5f) {
                beginPath();
                roundedRect(a, bar.y, b - a, bar.h, 4);
                fillColor(i == 2 || (row.bipolar && v > 0) ? theme::accent() : theme::warm());
                fill();
            }
            const char* sign = row.bipolar ? "%+.1f dB" : i == 2 ? "+%.1f dB" : "-%.1f dB";
            if (std::abs(v) < 0.05f)
                sign = "0.0 dB";
            drawText(*this, r.x + r.w, y, 11.0f, theme::text(), ALIGN_RIGHT | ALIGN_MIDDLE, fmt(sign, v).c_str());
        }
    }

    static constexpr int kStages = 7;
    float stage_[kStages] = {};
    float peakDb_ = -90.0f;
    mend::VoiceClean eq_; // used only to draw the EQ curve

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VoiceUI)
};

UI* createUI() { return new VoiceUI(); }

END_NAMESPACE_DISTRHO
