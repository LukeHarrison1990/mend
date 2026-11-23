// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#include "EditorWindow.hpp"

#include "mend/audio/AudioDevices.hpp"

#include "Spectrogram.hpp"
#include "Project.hpp"
#include "mend/VoiceClean.hpp"
#include "mend/ui/LogoPng.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>

START_NAMESPACE_DGL

using namespace mend::editor;
using mend::editor::ModuleId;
using namespace mend::ui;

namespace {
constexpr float kHeaderH = 56, kTransportH = 64, kPanelW = 360, kRulerH = 26, kOverviewH = 46;
constexpr float kMinW = 960, kMinH = 560;

std::string fmt(const char* f, double v)
{
    char b[64];
    std::snprintf(b, sizeof(b), f, v);
    return b;
}

float toNorm(const ToolParam& p, float v)
{
    v = std::clamp(v, p.min, p.max);
    if (p.kind == ToolParam::LogKnob)
        return std::log(v / p.min) / std::log(p.max / p.min);
    return (v - p.min) / (p.max - p.min);
}

float fromNorm(const ToolParam& p, float n)
{
    n = std::clamp(n, 0.0f, 1.0f);
    float v = p.kind == ToolParam::LogKnob ? p.min * std::pow(p.max / p.min, n) : p.min + n * (p.max - p.min);
    if (p.kind == ToolParam::IntKnob)
        v = std::round(v);
    return v;
}

// Which knob is shown dimmed (and with a live value) because a toggle
// above it puts it under automatic control.
bool knobIsAuto(ToolId id, int param, const ToolSettings& s)
{
    if (id == ToolId::DeHum && param == 1)
        return s.values[0] > 0.5f; // frequency vs auto-detect
    if (id == ToolId::DeClip && param == 1)
        return s.values[0] > 0.5f; // threshold vs auto threshold
    return false;
}

enum Icon { kIconPlay, kIconPause, kIconStop, kIconMinus, kIconPlus, kIconFit };
} // namespace

// ---------------------------------------------------------------------------

EditorWindow::EditorWindow(Application& app)
    : NanoStandaloneWindow(app)
    , chain_(defaultChain())
    , expanded_(static_cast<size_t>(ToolId::Count), false)
{
    loadSharedResources();
    {
        std::array<float, kLive> nan;
        nan.fill(NAN);
        std::array<float, kLive> zero {};
        const size_t n = static_cast<size_t>(ToolId::Count);
        liveTarget_.assign(n, nan);
        liveShown_.assign(n, nan);
        meterTarget_.assign(n, zero);
        meterShown_.assign(n, zero);
        meterLayout_.resize(n);
    }
    const double sf = Window::getScaleFactor();
    setTitle("Mend Editor");
    setResizable(true);
    setGeometryConstraints(static_cast<uint>(kMinW * sf), static_cast<uint>(kMinH * sf));
    addIdleCallback(this, 30);
    // open the commonly-needed tools by default
    expanded_[static_cast<int>(ToolId::DeNoise)] = true;
    modules_.resize(static_cast<size_t>(ModuleId::Count));
    for (int i = 0; i < static_cast<int>(ModuleId::Count); ++i)
        modules_[static_cast<size_t>(i)].values = defaultModuleValues(static_cast<ModuleId>(i));
    setStatus("Open an audio file to begin (Ctrl+O).");
    setupAudioPanel();
    done();
}

EditorWindow::~EditorWindow()
{
    removeIdleCallback(this);
    jobs_.cancel();
    jobs_.wait();
    spec_.quiesce();
    player_.stop();
    renderer_.cancel();
}

// ---- layout ----------------------------------------------------------------

void EditorWindow::showAtDefaultSize()
{
    // Sized after show(): sizing before the native window exists doesn't
    // reach the drawing surface on every platform.
    show();
    pendingInitialSize_ = 2; // applied from the idle callback once the window is mapped
}

double EditorWindow::scale() const
{
    const double s = Window::getScaleFactor();
    return s > 0.0 ? s : 1.0;
}

float EditorWindow::logicalW() const { return static_cast<float>(Window::getWidth() / scale()); }
float EditorWindow::logicalH() const { return static_cast<float>(Window::getHeight() / scale()); }

void EditorWindow::layout()
{
    const float W = logicalW(), H = logicalH();
    header_ = { 0, 0, W, kHeaderH };
    transport_ = { 0, H - kTransportH, W, kTransportH };
    panel_ = { W - kPanelW, kHeaderH, kPanelW, H - kHeaderH - kTransportH };
    panelFooter_ = { panel_.x, panel_.y + panel_.h - 96, panel_.w, 96 };
    panelList_ = { panel_.x, panel_.y + 40, panel_.w, panel_.h - 40 - panelFooter_.h };
    main_ = { 0, kHeaderH, W - kPanelW, H - kHeaderH - kTransportH };
    const float pad = 16, axisW = 44;
    toolbar_ = { main_.x + pad, main_.y + 10, main_.w - 2 * pad, 26 };
    ruler_ = { main_.x + pad, toolbar_.y + toolbar_.h + 6, main_.w - 2 * pad - axisW, kRulerH };
    overview_ = { main_.x + pad, main_.y + main_.h - kOverviewH - 12, ruler_.w, kOverviewH };
    wave_ = { ruler_.x, ruler_.y + ruler_.h, ruler_.w, overview_.y - 10 - (ruler_.y + ruler_.h) };
    { // whole physical pixels, so the cached images map 1:1 onto the screen
        const double sc = scale();
        auto snap = [sc](float v) { return static_cast<float>(std::round(v * sc) / sc); };
        wave_ = { snap(wave_.x), snap(wave_.y), snap(wave_.w), snap(wave_.h) };
    }
    axis_ = { wave_.x + wave_.w, wave_.y, axisW, wave_.h };
    // keep the view sensible when the window is resized
    if (doc_.isOpen() && lastWaveW_ > 0 && wave_.w != lastWaveW_) {
        const bool wasFit = viewStart_ <= 0.0 && lastWaveW_ * framesPerPx_ >= static_cast<double>(numFrames()) - 1.0;
        if (wasFit)
            fitView();
        else
            clampView();
    }
    lastWaveW_ = wave_.w;
}

// ---- small drawing helpers -------------------------------------------------

void EditorWindow::text(float x, float y, float size, const Color& c, int align, const std::string& s)
{
    fontSize(size);
    fillColor(c);
    textAlign(align);
    NanoVG::text(x, y, s.c_str(), nullptr);
}

bool EditorWindow::button(const Rect& r, const char* label, bool enabled, bool primary, std::function<void()> action)
{
    beginPath();
    roundedRect(r.x, r.y, r.w, r.h, r.h * 0.5f);
    if (primary && enabled)
        fillColor(theme::accent());
    else
        fillColor(enabled ? Color(42, 47, 56) : Color(30, 33, 39));
    fill();
    if (!primary || !enabled) {
        strokeColor(theme::panelEdge());
        strokeWidth(1.0f);
        stroke();
    }
    const Color tc = primary && enabled ? theme::well() : (enabled ? theme::text() : theme::textFaint());
    text(r.cx(), r.cy() + 0.5f, 13.0f, tc, ALIGN_CENTER | ALIGN_MIDDLE, label);
    if (enabled && action)
        hits_.push_back({ r, std::move(action) });
    return enabled;
}

void EditorWindow::iconButton(const Rect& r, int icon, bool enabled, bool active, std::function<void()> action)
{
    beginPath();
    roundedRect(r.x, r.y, r.w, r.h, 10);
    fillColor(active ? theme::accent() : (enabled ? Color(42, 47, 56) : Color(30, 33, 39)));
    fill();
    const Color ic = active ? theme::well() : (enabled ? theme::text() : theme::textFaint());
    const float cx = r.cx(), cy = r.cy(), s = std::min(r.w, r.h) * 0.22f;
    beginPath();
    switch (icon) {
    case kIconPlay:
        moveTo(cx - s * 0.8f, cy - s);
        lineTo(cx + s * 1.1f, cy);
        lineTo(cx - s * 0.8f, cy + s);
        closePath();
        break;
    case kIconPause:
        rect(cx - s, cy - s, s * 0.7f, s * 2);
        rect(cx + s * 0.3f, cy - s, s * 0.7f, s * 2);
        break;
    case kIconStop:
        rect(cx - s * 0.85f, cy - s * 0.85f, s * 1.7f, s * 1.7f);
        break;
    case kIconMinus:
        rect(cx - s, cy - 1.2f, s * 2, 2.4f);
        break;
    case kIconPlus:
        rect(cx - s, cy - 1.2f, s * 2, 2.4f);
        rect(cx - 1.2f, cy - s, 2.4f, s * 2);
        break;
    case kIconFit:
        rect(cx - s * 1.2f, cy - 1.2f, s * 2.4f, 2.4f);
        rect(cx - s * 1.2f, cy - s, 2.4f, s * 2);
        rect(cx + s * 1.2f - 2.4f, cy - s, 2.4f, s * 2);
        break;
    }
    fillColor(ic);
    fill();
    if (enabled && action)
        hits_.push_back({ r, std::move(action) });
}

// ---- top-level draw --------------------------------------------------------

void EditorWindow::onNanoDisplay()
{
    // An exception must never escape into the windowing code (C frames on
    // Windows can't unwind it, and the app would vanish): log and carry on.
    try {
        drawFrame();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Mend: drawing failed: %s\n", e.what());
    }
}

void EditorWindow::drawFrame()
{
    static const bool profile = std::getenv("MEND_PROFILE") != nullptr;
    const auto t0 = std::chrono::steady_clock::now();
    struct Report {
        std::chrono::steady_clock::time_point t0;
        bool on;
        ~Report()
        {
            if (on)
                std::fprintf(stderr, "draw %.2f ms\n", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
        }
    } report { t0, profile };
    hits_.clear();
    knobHits_.clear();
    if (Window::getWidth() != lastPhysW_ || Window::getHeight() != lastPhysH_) {
        // some GL drivers present the first frame after a resize at the old size
        lastPhysW_ = Window::getWidth();
        lastPhysH_ = Window::getHeight();
        repaintAfterResize_ = true;
    }
    layout();
    fontFace(NANOVG_DEJAVU_SANS_TTF);

    beginPath();
    rect(0, 0, static_cast<float>(Window::getWidth()), static_cast<float>(Window::getHeight()));
    fillColor(theme::background());
    fill();

    save();
    const float s = static_cast<float>(scale());
    NanoVG::scale(s, s); // everything below is in logical units, drawn at full resolution

    auto lap = [&](const char* what) {
        static auto last = std::chrono::steady_clock::now();
        if (profile) {
            const auto now = std::chrono::steady_clock::now();
            std::fprintf(stderr, "  %s %.2f\n", what, std::chrono::duration<double, std::milli>(now - last).count());
            last = now;
        }
    };
    lap("start");
    drawHeader();
    if (doc_.isOpen()) {
        drawToolbar();
        drawRuler();
        lap("ruler");
        drawWaveform();
        lap("wave");
        drawOverview();
        lap("overview");
    } else {
        drawEmptyState();
    }
    drawPanel();
    lap("panel");
    drawTransport();
    lap("transport");
    if (choiceList_.value != nullptr)
        drawChoiceList();
    if (fileJob_ != nullptr) {
        hits_.clear(); // only the overlay's Cancel works while a file opens
        drawFileJobOverlay();
    }
    if (audioPanel_.isOpen()) {
        hits_.clear(); // the panel takes all clicks
        knobHits_.clear();
        audioPanel_.draw(*this, logicalW(), logicalH());
    }
    drawPrompt();
    restore();
}

void EditorWindow::drawHeader()
{
    beginPath();
    rect(header_.x, header_.y, header_.w, header_.h);
    fillColor(Color(21, 23, 28));
    fill();
    beginPath();
    moveTo(0, header_.h - 0.5f);
    lineTo(header_.w, header_.h - 0.5f);
    strokeColor(theme::panelEdge());
    strokeWidth(1.0f);
    stroke();

    if (!logo_.isValid())
        logo_ = createImageFromMemory(mend::ui::kLogoPng, mend::ui::kLogoPngSize, 0);
    if (logo_.isValid()) {
        beginPath();
        rect(18, 13, 30, 30);
        fillPaint(imagePattern(18, 13, 30, 30, 0.0f, logo_, 1.0f));
        fill();
    }
    const float lx = 36; // everything after the icon shifts right
    text(22 + lx, 30, 22.0f, theme::text(), ALIGN_LEFT | ALIGN_MIDDLE, "mend");
    beginPath();
    circle(83 + lx, 36, 3.2f);
    fillColor(theme::accent());
    fill();
    text(98 + lx, 31, 15.0f, theme::textDim(), ALIGN_LEFT | ALIGN_MIDDLE, "Editor");

    if (doc_.isOpen()) {
        const auto& a = doc_.audio();
        std::string name = projectPath_.empty() ? doc_.displayName() : projectPath_.substr(projectPath_.find_last_of("/\\") + 1);
        if (dirty())
            name += "  \xe2\x80\xa2";
        text(180 + lx, 24, 14.0f, theme::text(), ALIGN_LEFT | ALIGN_MIDDLE, name);
        const std::string info = fmt("%.1f kHz", a.sampleRate / 1000.0) + "  \xc2\xb7  "
            + (a.numChannels() == 1 ? std::string("mono") : a.numChannels() == 2 ? std::string("stereo")
                                                                                    : fmt("%.0f channels", a.numChannels()))
            + "  \xc2\xb7  " + timeString(static_cast<double>(a.numFrames()));
        text(180 + lx, 41, 11.5f, theme::textDim(), ALIGN_LEFT | ALIGN_MIDDLE, info);
    }

    const bool idle = !busy();
    float x = header_.w - 16;
    auto right = [&](float w, const char* label, bool en, bool primary, std::function<void()> fn) {
        x -= w;
        button({ x, 13, w, 30 }, label, en, primary, std::move(fn));
        x -= 8;
    };
    right(76, "Redo", idle && doc_.canRedo(), false, [this] { redo(); });
    right(76, "Undo", idle && doc_.canUndo(), false, [this] { undo(); });
    right(110, "Export WAV", idle && doc_.isOpen(), false, [this] { chooseExport(); });
    right(80, "Save", idle && doc_.isOpen(), false, [this] { chooseSaveProject(false); });
    right(80, "Open", idle, !doc_.isOpen(), [this] { chooseOpen(); });
    x -= 8;
    right(80, "Audio", true, false, [this] {
        audioPanel_.open();
        repaint();
    });
}

void EditorWindow::drawEmptyState()
{
    const Rect r { main_.x + 16, main_.y + 16, main_.w - 32, main_.h - 32 };
    beginPath();
    roundedRect(r.x, r.y, r.w, r.h, 14);
    fillColor(theme::panel());
    fill();
    strokeColor(theme::panelEdge());
    strokeWidth(1.0f);
    stroke();
    text(r.cx(), r.cy() - 52, 22.0f, theme::text(), ALIGN_CENTER | ALIGN_MIDDLE, "Open an audio file to repair");
    text(r.cx(), r.cy() - 22, 13.0f, theme::textDim(), ALIGN_CENTER | ALIGN_MIDDLE,
         "WAV, AIFF, FLAC, MP3, Ogg Vorbis, Opus, AAC, ALAC  \xc2\xb7  any sample rate  \xc2\xb7  mono, stereo or more");
    button({ r.cx() - 90, r.cy() + 10, 180, 40 }, "Open file...", true, true, [this] { chooseOpen(); });
    text(r.cx(), r.cy() + 74, 11.5f, theme::textFaint(), ALIGN_CENTER | ALIGN_MIDDLE,
         "Space play/stop  \xc2\xb7  wheel zoom  \xc2\xb7  Shift+wheel scroll  \xc2\xb7  drag to select  \xc2\xb7  Ctrl+Z undo");
}

// ---- waveform area -------------------------------------------------------

double EditorWindow::frameAtX(float x) const { return viewStart_ + (x - wave_.x) * framesPerPx_; }
float EditorWindow::xAtFrame(double f) const { return wave_.x + static_cast<float>((f - viewStart_) / framesPerPx_); }

void EditorWindow::fitView()
{
    if (!doc_.isOpen() || wave_.w <= 0)
        return;
    framesPerPx_ = static_cast<double>(numFrames()) / wave_.w;
    viewStart_ = 0;
}

void EditorWindow::clampView()
{
    const double n = static_cast<double>(numFrames());
    const double maxFpp = n / std::max(1.0f, wave_.w);
    framesPerPx_ = std::clamp(framesPerPx_, 0.05, std::max(0.05, maxFpp));
    const double visible = wave_.w * framesPerPx_;
    viewStart_ = std::clamp(viewStart_, 0.0, std::max(0.0, n - visible));
}

void EditorWindow::zoomAround(double factor, float x)
{
    const double anchor = frameAtX(x);
    framesPerPx_ *= factor;
    clampView();
    viewStart_ = anchor - (x - wave_.x) * framesPerPx_;
    clampView();
    repaint();
}

std::string EditorWindow::timeString(double frames, bool withMs) const
{
    const double fs = doc_.isOpen() ? doc_.audio().sampleRate : 48000.0;
    const double t = std::max(0.0, frames / fs);
    const int m = static_cast<int>(t / 60.0);
    const double s = t - m * 60.0;
    char b[32];
    if (withMs)
        std::snprintf(b, sizeof(b), "%d:%06.3f", m, s);
    else
        std::snprintf(b, sizeof(b), "%d:%02d", m, static_cast<int>(s));
    return b;
}

void EditorWindow::drawRuler()
{
    const double fs = doc_.audio().sampleRate;
    const double secPerPx = framesPerPx_ / fs;
    static const double steps[] = { 0.001, 0.002, 0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600 };
    double step = steps[sizeof(steps) / sizeof(steps[0]) - 1];
    for (double st : steps)
        if (st / secPerPx >= 90.0) {
            step = st;
            break;
        }
    const double t0 = viewStart_ / fs, t1 = (viewStart_ + wave_.w * framesPerPx_) / fs;
    save();
    scissor(ruler_.x, ruler_.y, ruler_.w, ruler_.h);
    for (double t = std::floor(t0 / step) * step; t <= t1; t += step) {
        const float x = xAtFrame(t * fs);
        beginPath();
        moveTo(x, ruler_.y + ruler_.h - 8);
        lineTo(x, ruler_.y + ruler_.h);
        strokeColor(theme::textFaint());
        strokeWidth(1.0f);
        stroke();
        const int m = static_cast<int>(t / 60.0);
        const double s = t - m * 60.0;
        std::string label = step < 1.0 ? fmt(step < 0.01 ? "%.3f" : step < 0.1 ? "%.2f" : "%.1f", s) : fmt("%02.0f", s);
        label = fmt("%.0f:", m) + (s < 10 && step >= 1.0 ? label : (s < 10 ? "0" + label : label));
        text(x + 4, ruler_.y + 9, 10.5f, theme::textDim(), ALIGN_LEFT | ALIGN_MIDDLE, label);
        for (int k = 1; k < 5; ++k) {
            const float xm = xAtFrame((t + step * k / 5.0) * fs);
            beginPath();
            moveTo(xm, ruler_.y + ruler_.h - 4);
            lineTo(xm, ruler_.y + ruler_.h);
            strokeColor(theme::grid());
            stroke();
        }
    }
    restore();
}

void EditorWindow::laneGeometry(int nch, int& wx, int& wy, int& ww, int& laneH, int& gap) const
{
    const double s = scale();
    wx = static_cast<int>(std::lround(wave_.x * s));
    wy = static_cast<int>(std::lround(wave_.y * s));
    ww = std::max(1, static_cast<int>(std::lround(wave_.w * s)));
    const int wh = std::max(1, static_cast<int>(std::lround(wave_.h * s)));
    gap = static_cast<int>(std::lround(6 * s));
    laneH = std::max(8, (wh - gap * (nch - 1)) / std::max(1, nch));
}

void EditorWindow::updatePeaks()
{
    const auto& a = doc_.audio();
    if (peaksVersion_ != doc_.version()) {
        if (peaksVersion_ == 0 || doc_.changedLength() >= a.numFrames())
            peaks_.rebuild(a);
        else
            peaks_.update(a, doc_.changedStart(), doc_.changedLength());
        peaksVersion_ = doc_.version();
    }
}

void EditorWindow::uploadImage(std::unique_ptr<NanoImage>& img, int w, int h, const std::vector<uint8_t>& rgba, int flags)
{
    if (!img || !img->isValid() || img->getSize().getWidth() != static_cast<uint>(w)
        || img->getSize().getHeight() != static_cast<uint>(h)) {
        img.reset(new NanoImage(createImageFromRGBA(static_cast<uint>(w), static_cast<uint>(h), rgba.data(), flags)));
    } else {
        img->update(rgba.data());
    }
}

void EditorWindow::uploadAlphaImage(std::unique_ptr<NanoImage>& img, int w, int h, const std::vector<uint8_t>& alpha)
{
    if (!img || !img->isValid() || img->getSize().getWidth() != static_cast<uint>(w)
        || img->getSize().getHeight() != static_cast<uint>(h)) {
        img.reset(new NanoImage(createImageFromRawMemory(static_cast<uint>(w), static_cast<uint>(h), alpha.data(),
                                                         1 << 5 /* nearest */, kImageFormatGrayscale)));
    } else {
        img->update(alpha.data());
    }
}

void EditorWindow::drawWaveform()
{
    const auto& a = doc_.audio();
    updatePeaks();
    const int nch = a.numChannels();
    const double s = scale();
    int wx, wy, ww, laneH, gap;
    laneGeometry(nch, wx, wy, ww, laneH, gap);
    const double fpc = framesPerPx_ / s; // frames per physical column
    const bool showSpec = showSpectrogram();
    const bool showWave = showWaveform();
    // Blend: the spectrogram fades in over the first half of the slider,
    // the waveform fades out over the second.
    const float specAlpha = std::min(1.0f, viewBlend_ * 2.0f);
    const float waveAlpha = viewBlend_ <= 0.5f ? 0.9f - 0.35f * (viewBlend_ / 0.5f) : 0.55f * (1.0f - viewBlend_) / 0.5f;
    const bool overlay = viewBlend_ > 0.15f; // light waveform over the spectrogram
    const bool sampleMode = fpc < 1.0; // zoomed in far enough to draw the samples as a line
    const auto L = [s](int px) { return static_cast<float>(px / s); };

    // -- spectrogram: ask for the current view; show the newest finished one
    if (showSpec) {
        mend::editor::SpecView want;
        want.viewStart = viewStart_;
        want.framesPerCol = fpc;
        want.cols = ww;
        want.rows = laneH;
        want.scale = freqScale_;
        want.floorDb = specFloorDb_;
        want.docVersion = doc_.version();
        if (want != specRequested_) {
            spec_.request(&a, want);
            specRequested_ = want;
        }
        std::vector<std::vector<uint8_t>> lanes;
        mend::editor::SpecView got;
        if (spec_.take(got, lanes) && static_cast<int>(lanes.size()) == nch) {
            specImg_.resize(static_cast<size_t>(nch));
            for (int c = 0; c < nch; ++c)
                uploadImage(specImg_[static_cast<size_t>(c)], got.cols, got.rows, lanes[static_cast<size_t>(c)], 0);
            specShown_ = got;
        }
    }

    // -- waveform image (only rebuilt when the view or the audio changes)
    if (showWave && !sampleMode) {
        // A tile up to three views wide, starting a whole number of columns
        // before the view so the pixels stay put while it slides.
        const int tileW = std::min(ww * 3, std::max(ww, 8192));
        const int margin = (tileW - ww) / 2;
        const double viewEnd = viewStart_ + ww * fpc;
        const bool fits = waveKey_.fpc == fpc && waveKey_.tileW == tileW && waveKey_.h == laneH
            && waveKey_.version == doc_.version() && viewStart_ >= waveKey_.tileStart
            && viewEnd <= waveKey_.tileStart + tileW * fpc && static_cast<int>(waveImg_.size()) == nch;
        if (!fits) {
            WaveKey key { viewStart_ - margin * fpc, fpc, tileW, laneH, doc_.version() };
            waveImg_.resize(static_cast<size_t>(nch));
            for (int c = 0; c < nch; ++c) {
                mend::editor::rasterWaveformAlpha(a, peaks_, c, key.tileStart, fpc, tileW, laneH, waveRgba_);
                uploadAlphaImage(waveImg_[static_cast<size_t>(c)], tileW, laneH, waveRgba_);
            }
            waveKey_ = key;
        }
    }

    beginPath();
    roundedRect(wave_.x, wave_.y, wave_.w, wave_.h, 8);
    fillColor(theme::well());
    fill();

    save();
    scissor(wave_.x, wave_.y, wave_.w, wave_.h);
    for (int c = 0; c < nch; ++c) {
        const float lx = L(wx), ly = L(wy + c * (laneH + gap)), lw = L(ww), lh = L(laneH);
        const float mid = ly + lh * 0.5f, amp = lh * 0.47f;

        if (showSpec && c < static_cast<int>(specImg_.size()) && specImg_[static_cast<size_t>(c)]) {
            // place the image by the frames it was drawn for, so a stale one
            // still lines up (stretched) while the new one is computed
            const float x0 = xAtFrame(specShown_.viewStart);
            const float w0 = static_cast<float>(specShown_.cols * specShown_.framesPerCol / framesPerPx_);
            save();
            scissor(lx, ly, lw, lh);
            beginPath();
            rect(lx, ly, lw, lh);
            fillColor(Color(10, 11, 14, specAlpha));
            fill();
            beginPath();
            rect(std::max(lx, x0), ly, std::min(lx + lw, x0 + w0) - std::max(lx, x0), lh);
            fillPaint(imagePattern(x0, ly, w0, lh, 0.0f, *specImg_[static_cast<size_t>(c)], specAlpha));
            fill();
            restore();
        }
        if (showWave) {
            beginPath();
            moveTo(lx, mid);
            lineTo(lx + lw, mid);
            strokeColor(showSpec ? Color(255, 255, 255, 0.12f) : theme::grid());
            strokeWidth(1.0f);
            stroke();
        }
        if (c > 0) {
            beginPath();
            moveTo(lx, ly - L(gap) * 0.5f);
            lineTo(lx + lw, ly - L(gap) * 0.5f);
            strokeColor(theme::panelEdge());
            strokeWidth(1.0f);
            stroke();
        }
        if (showWave) {
            if (!sampleMode) {
                if (c < static_cast<int>(waveImg_.size()) && waveImg_[static_cast<size_t>(c)]) {
                    // the tile, placed by the frame it starts at and tinted
                    const float tx = xAtFrame(waveKey_.tileStart);
                    const float tw = static_cast<float>(waveKey_.tileW * waveKey_.fpc / framesPerPx_);
                    Paint p = imagePattern(tx, ly, tw, lh, 0.0f, *waveImg_[static_cast<size_t>(c)], waveAlpha);
                    p.innerColor = overlay ? Color(225, 240, 255, waveAlpha) : Color(64, 214, 196, waveAlpha);
                    p.outerColor = p.innerColor;
                    beginPath();
                    rect(lx, ly, lw, lh);
                    fillPaint(p);
                    fill();
                }
            } else {
                const size_t f0 = static_cast<size_t>(std::max(0.0, std::floor(viewStart_)));
                const size_t f1 = std::min(a.numFrames(), static_cast<size_t>(std::ceil(frameAtX(wave_.x + wave_.w))) + 1);
                const float* x = a.channels[static_cast<size_t>(c)].data();
                Color lc = overlay ? Color(225, 240, 255) : theme::accent();
                lc.alpha = std::min(1.0f, waveAlpha * 1.4f);
                beginPath();
                for (size_t f = f0; f < f1; ++f) {
                    const float px = xAtFrame(static_cast<double>(f)), py = mid - std::clamp(x[f], -1.0f, 1.0f) * amp;
                    if (f == f0)
                        moveTo(px, py);
                    else
                        lineTo(px, py);
                }
                strokeColor(lc);
                strokeWidth(1.5f);
                stroke();
                if (framesPerPx_ < 0.2)
                    for (size_t f = f0; f < f1; ++f) {
                        beginPath();
                        circle(xAtFrame(static_cast<double>(f)), mid - std::clamp(x[f], -1.0f, 1.0f) * amp, 2.2f);
                        fillColor(lc);
                        fill();
                    }
            }
        }
        text(lx + 8, ly + 11, 10.0f, showSpec ? Color(255, 255, 255, 0.6f) : theme::textFaint(),
             ALIGN_LEFT | ALIGN_MIDDLE, nch == 2 ? (c == 0 ? "L" : "R") : fmt("%.0f", c + 1));
    }

    // selection, cursor and playhead over everything
    if (hasSelection() && boxActive_ && showSpec) {
        // spectral selection: a box in each lane, the rest of the time range dimmed lightly
        const float x0 = xAtFrame(static_cast<double>(selStart_)), x1 = xAtFrame(static_cast<double>(selEnd_));
        for (int c = 0; c < nch; ++c) {
            const float ly = L(wy + c * (laneH + gap)), lh = L(laneH);
            const float y0 = ly + static_cast<float>(mend::editor::specPosOf(freqScale_, a.sampleRate, boxHiHz_)) * lh;
            const float y1 = ly + static_cast<float>(mend::editor::specPosOf(freqScale_, a.sampleRate, boxLoHz_)) * lh;
            beginPath();
            rect(x0, y0, x1 - x0, std::max(1.0f, y1 - y0));
            fillColor(Color(255, 255, 255, 0.12f));
            fill();
            strokeColor(Color(242, 166, 90));
            strokeWidth(1.5f);
            stroke();
        }
    } else if (hasSelection()) {
        const float x0 = std::max(wave_.x, xAtFrame(static_cast<double>(selStart_)));
        const float x1 = std::min(wave_.x + wave_.w, xAtFrame(static_cast<double>(selEnd_)));
        if (x1 > x0) {
            beginPath();
            rect(x0, wave_.y, x1 - x0, wave_.h);
            fillColor(showSpec ? Color(255, 255, 255, 0.10f) : Color(64, 214, 196, 0.12f));
            fill();
        }
        for (size_t f : { selStart_, selEnd_ }) {
            const float x = xAtFrame(static_cast<double>(f));
            beginPath();
            moveTo(x, wave_.y);
            lineTo(x, wave_.y + wave_.h);
            strokeColor(showSpec ? Color(255, 255, 255, 0.8f) : theme::accent());
            strokeWidth(1.0f);
            stroke();
        }
    }
    {
        const float x = xAtFrame(static_cast<double>(cursor_));
        beginPath();
        moveTo(x, wave_.y);
        lineTo(x, wave_.y + wave_.h);
        strokeColor(Color(255, 255, 255, 0.35f));
        strokeWidth(1.0f);
        stroke();
    }
    if (player_.playing()) {
        const float x = xAtFrame(static_cast<double>(std::min(player_.position(), numFrames())));
        beginPath();
        moveTo(x, wave_.y);
        lineTo(x, wave_.y + wave_.h);
        strokeColor(theme::warm());
        strokeWidth(1.5f);
        stroke();
    }
    restore();

    drawAxis(nch, wy, laneH, gap);

    drawBusyOverlay();
}

void EditorWindow::drawBusyOverlay()
{
    if (!busy())
        return;
    const bool job = jobs_.busy();
    const std::string stage = job ? jobs_.stage() : renderer_.stage();
    const float prog = job ? jobs_.progress() : renderer_.progress();
    const Rect b { wave_.cx() - 170, wave_.cy() - 40, 340, 80 };
    beginPath();
    roundedRect(b.x, b.y, b.w, b.h, 12);
    fillColor(Color(17, 19, 23, 0.92f));
    fill();
    strokeColor(theme::panelEdge());
    stroke();
    text(b.x + 16, b.y + 22, 13.0f, theme::text(), ALIGN_LEFT | ALIGN_MIDDLE, stage + "...");
    text(b.x + b.w - 16, b.y + 22, 13.0f, theme::textDim(), ALIGN_RIGHT | ALIGN_MIDDLE, fmt("%.0f%%", prog * 100.0));
    beginPath();
    roundedRect(b.x + 16, b.y + 42, b.w - 112, 8, 4);
    fillColor(theme::well());
    fill();
    beginPath();
    roundedRect(b.x + 16, b.y + 42, std::max(8.0f, (b.w - 112) * prog), 8, 4);
    fillColor(theme::accent());
    fill();
    button({ b.x + b.w - 86, b.y + 32, 70, 28 }, "Cancel", true, false, [this] {
        renderer_.cancel();
        jobs_.cancel();
    });
}

// Right-hand scale: frequency (Hz) for the spectrogram, level (dB) otherwise.
void EditorWindow::drawAxis(int nch, int wy, int laneH, int gap)
{
    const double s = scale();
    const float ax = axis_.x + 6;
    const double fs = doc_.audio().sampleRate;
    for (int c = 0; c < nch; ++c) {
        const float ly = static_cast<float>((wy + c * (laneH + gap)) / s), lh = static_cast<float>(laneH / s);
        auto tick = [&](float y, const std::string& label) {
            if (y < ly + 4 || y > ly + lh - 4)
                return;
            beginPath();
            moveTo(axis_.x, y);
            lineTo(axis_.x + 4, y);
            strokeColor(theme::textFaint());
            strokeWidth(1.0f);
            stroke();
            text(ax, y, 10.0f, theme::textDim(), ALIGN_LEFT | ALIGN_MIDDLE, label);
        };
        if (viewBlend_ < 0.5f) { // the axis follows whichever view dominates
            const float mid = ly + lh * 0.5f, amp = lh * 0.47f;
            tick(mid, "-\xe2\x88\x9e");
            for (float db : { 0.0f, -6.0f, -12.0f }) {
                const float g = std::pow(10.0f, db / 20.0f);
                tick(mid - g * amp, fmt("%.0f", db));
                tick(mid + g * amp, fmt("%.0f", db));
            }
        } else {
            static const double marks[] = { 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000, 40000 };
            float lastY = -1e9f;
            for (double hz : marks) {
                if (hz >= fs * 0.5)
                    break;
                const float y = ly + static_cast<float>(mend::editor::specPosOf(freqScale_, fs, hz)) * lh;
                if (std::abs(y - lastY) < 16) // too crowded on this scale
                    continue;
                tick(y, hz >= 1000 ? fmt("%.0fk", hz / 1000) : fmt("%.0f", hz));
                lastY = y;
            }
        }
    }
    text(axis_.cx(), ruler_.y + 9, 9.5f, theme::textFaint(), ALIGN_CENTER | ALIGN_MIDDLE,
         viewBlend_ < 0.5f ? "dB" : "Hz");
}

void EditorWindow::drawToolbar()
{
    // view: a slider from waveform to spectrogram
    const float h = 26, y = toolbar_.y;
    const float right = toolbar_.x + toolbar_.w;
    float x = toolbar_.x;
    const float sliderW = toolbar_.w < 760 ? 246 : 288;
    {
        const Rect r { x, y, sliderW, h };
        beginPath();
        roundedRect(r.x, r.y, r.w, r.h, h * 0.5f);
        fillColor(Color(30, 33, 39));
        fill();
        strokeColor(theme::panelEdge());
        strokeWidth(1.0f);
        stroke();
        const float labelW = toolbar_.w < 760 ? 70 : 84;
        text(r.x + 12, r.cy() + 0.5f, 11.5f, viewBlend_ < 0.5f ? theme::text() : theme::textDim(), ALIGN_LEFT | ALIGN_MIDDLE,
             "Waveform");
        text(r.x + r.w - 12, r.cy() + 0.5f, 11.5f, viewBlend_ > 0.5f ? theme::text() : theme::textDim(),
             ALIGN_RIGHT | ALIGN_MIDDLE, "Spectrogram");
        blendTrack_ = { r.x + labelW, r.y, r.w - labelW - (toolbar_.w < 760 ? 86 : 100), h };
        const float ty = r.cy();
        beginPath();
        moveTo(blendTrack_.x, ty);
        lineTo(blendTrack_.x + blendTrack_.w, ty);
        strokeColor(theme::knobTrack());
        strokeWidth(4.0f);
        lineCap(ROUND);
        stroke();
        beginPath();
        moveTo(blendTrack_.x, ty);
        lineTo(blendTrack_.x + blendTrack_.w * viewBlend_, ty);
        strokeColor(theme::accent());
        stroke();
        for (float t : { 0.0f, 0.5f, 1.0f }) { // detents
            beginPath();
            circle(blendTrack_.x + blendTrack_.w * t, ty, 1.6f);
            fillColor(theme::textFaint());
            fill();
        }
        beginPath();
        circle(blendTrack_.x + blendTrack_.w * viewBlend_, ty, 7.0f);
        fillColor(theme::text());
        fill();
    }
    if (!showSpectrogram())
        return;
    x += sliderW + 12;
    { // selection tool: plain time range, or a time-frequency box
        const char* tn[] = { "Time", "Box" };
        const float tw = 54;
        beginPath();
        roundedRect(x, y, tw * 2, h, h * 0.5f);
        fillColor(Color(30, 33, 39));
        fill();
        strokeColor(theme::panelEdge());
        strokeWidth(1.0f);
        stroke();
        for (int i = 0; i < 2; ++i) {
            const Rect r { x + i * tw, y, tw, h };
            const bool on = static_cast<int>(selectTool_) == i;
            if (on) {
                beginPath();
                roundedRect(r.x + 2, r.y + 2, r.w - 4, r.h - 4, (h - 4) * 0.5f);
                fillColor(Color(242, 166, 90));
                fill();
            }
            text(r.cx(), r.cy() + 0.5f, 12.0f, on ? theme::well() : theme::textDim(), ALIGN_CENTER | ALIGN_MIDDLE, tn[i]);
            hits_.push_back({ r, [this, i] {
                                 selectTool_ = static_cast<SelectTool>(i);
                                 repaint();
                             } });
        }
        x += tw * 2 + 12;
    }
    if (x + 112 > right)
        return;
    const char* scaleName = freqScale_ == mend::editor::FreqScale::Linear ? "Scale: Linear"
        : freqScale_ == mend::editor::FreqScale::Mel                       ? "Scale: Mel"
                                                                            : "Scale: Log";
    button({ x, y, 112, h }, scaleName, true, false, [this] {
        using FS = mend::editor::FreqScale;
        freqScale_ = freqScale_ == FS::Mel ? FS::Log : freqScale_ == FS::Log ? FS::Linear : FS::Mel;
        repaint();
    });
    x += 120;
    if (x + 112 > right)
        return;
    button({ x, y, 112, h }, fmt("Range: %.0f dB", -specFloorDb_).c_str(), true, false, [this] {
        specFloorDb_ = specFloorDb_ <= -140.0f ? -80.0f : specFloorDb_ - 15.0f;
        repaint();
    });
    x += 120;
    if (x + 124 > right)
        return;
    // colour key
    const Rect key { x + 6, y + 8, 110, 10 };
    for (int i = 0; i < 22; ++i) {
        uint8_t r, g, b;
        mend::editor::specColour(i / 21.0f, r, g, b);
        beginPath();
        rect(key.x + key.w * i / 22.0f, key.y, key.w / 22.0f + 0.5f, key.h);
        fillColor(Color(r, g, b));
        fill();
    }
    text(key.x - 2, key.cy() + 12, 9.0f, theme::textFaint(), ALIGN_LEFT | ALIGN_MIDDLE, fmt("%.0f", specFloorDb_));
    text(key.x + key.w + 2, key.cy() + 12, 9.0f, theme::textFaint(), ALIGN_RIGHT | ALIGN_MIDDLE, "0 dB");
}

void EditorWindow::drawOverview()
{
    const auto& a = doc_.audio();
    beginPath();
    roundedRect(overview_.x, overview_.y, overview_.w, overview_.h, 6);
    fillColor(theme::well());
    fill();
    const double fppAll = static_cast<double>(a.numFrames()) / overview_.w;
    const float mid = overview_.cy(), amp = overview_.h * 0.45f;
    const int cols = std::max(0, static_cast<int>(overview_.w));
    if (cols == 0 || a.numFrames() == 0)
        return;
    if (overviewVersion_ != doc_.version() || static_cast<int>(overviewLo_.size()) != cols) {
        overviewLo_.assign(static_cast<size_t>(cols), 0.0f);
        overviewHi_.assign(static_cast<size_t>(cols), 0.0f);
        for (int i = 0; i < cols; ++i) {
            float lo = 0, hi = 0;
            for (int c = 0; c < a.numChannels(); ++c) {
                float mn, mx;
                peaks_.range(a, c, static_cast<size_t>(i * fppAll), static_cast<size_t>((i + 1) * fppAll), mn, mx);
                lo = std::min(lo, mn);
                hi = std::max(hi, mx);
            }
            overviewLo_[static_cast<size_t>(i)] = std::clamp(lo, -1.0f, 1.0f);
            overviewHi_[static_cast<size_t>(i)] = std::clamp(hi, -1.0f, 1.0f);
        }
        overviewVersion_ = doc_.version();
    }
    beginPath();
    for (int i = 0; i < cols; ++i) {
        moveTo(overview_.x + i + 0.5f, mid - overviewHi_[static_cast<size_t>(i)] * amp);
        lineTo(overview_.x + i + 0.5f, mid - overviewLo_[static_cast<size_t>(i)] * amp);
    }
    strokeColor(Color(140, 147, 160, 0.6f));
    strokeWidth(1.0f);
    stroke();

    const float vx = overview_.x + static_cast<float>(viewStart_ / fppAll);
    const float vw = std::max(6.0f, static_cast<float>(wave_.w * framesPerPx_ / fppAll));
    beginPath();
    roundedRect(vx, overview_.y, std::min(vw, overview_.x + overview_.w - vx), overview_.h, 5);
    fillColor(Color(64, 214, 196, 0.12f));
    fill();
    strokeColor(theme::accent());
    strokeWidth(1.0f);
    stroke();
    if (player_.playing()) {
        const float px = overview_.x + static_cast<float>(player_.position() / fppAll);
        beginPath();
        moveTo(px, overview_.y);
        lineTo(px, overview_.y + overview_.h);
        strokeColor(theme::warm());
        stroke();
    }
}

// ---- transport -----------------------------------------------------------

void EditorWindow::drawTransport()
{
    const Rect& t = transport_;
    beginPath();
    rect(t.x, t.y, t.w, t.h);
    fillColor(Color(21, 23, 28));
    fill();
    beginPath();
    moveTo(0, t.y + 0.5f);
    lineTo(t.w, t.y + 0.5f);
    strokeColor(theme::panelEdge());
    strokeWidth(1.0f);
    stroke();

    const bool open = doc_.isOpen();
    const float cy = t.y + t.h * 0.5f;
    iconButton({ 16, cy - 20, 52, 40 }, player_.playing() ? kIconPause : kIconPlay, open, player_.playing(),
               [this] { togglePlay(); });
    iconButton({ 76, cy - 20, 44, 40 }, kIconStop, open, false, [this] { stopPlayback(); });

    const double pos = player_.playing() ? static_cast<double>(std::min(player_.position(), numFrames()))
                                         : static_cast<double>(cursor_);
    text(140, cy - 7, 20.0f, theme::text(), ALIGN_LEFT | ALIGN_MIDDLE, open ? timeString(pos) : "0:00.000");
    text(140, cy + 13, 10.5f, theme::textFaint(), ALIGN_LEFT | ALIGN_MIDDLE, "POSITION");

    if (hasSelection()) {
        text(290, cy - 7, 13.0f, theme::text(), ALIGN_LEFT | ALIGN_MIDDLE,
             timeString(static_cast<double>(selStart_)) + "  \xe2\x80\x93  " + timeString(static_cast<double>(selEnd_)));
        text(290, cy + 13, 10.5f, theme::textFaint(), ALIGN_LEFT | ALIGN_MIDDLE,
             "SELECTION  " + fmt("%.3f s", (selEnd_ - selStart_) / doc_.audio().sampleRate)
                 + (boxActive_ ? fmt("   %.0f", boxLoHz_) + fmt("\xe2\x80\x93%.0f Hz", boxHiHz_) : std::string()));
    } else {
        text(290, cy - 7, 13.0f, theme::textDim(), ALIGN_LEFT | ALIGN_MIDDLE, open ? "Whole file" : "");
        text(290, cy + 13, 10.5f, theme::textFaint(), ALIGN_LEFT | ALIGN_MIDDLE, open ? "SELECTION  (drag on the waveform)" : "");
    }

    // preview switch
    Toggle tg;
    const float tgX = t.w >= 1100 ? 540.0f : 470.0f;
    tg.bounds = { tgX, cy - 15, 196, 30 };
    tg.label = "Preview tools";
    tg.on = previewOn_;
    tg.draw(*this);
    hits_.push_back({ tg.bounds, [this] {
                         previewOn_ = !previewOn_;
                         settingsChanged(true);
                     } });

    // status (clipped so it never runs under the zoom buttons)
    save();
    scissor(tgX + 212, t.y, std::max(0.0f, t.w - 16 - 120 - 16 - 116 - 12 - (tgX + 212)), t.h);
    text(tgX + 212, cy, 12.0f, statusError_ ? theme::warm() : theme::textDim(), ALIGN_LEFT | ALIGN_MIDDLE, status_);
    restore();

    // zoom + meters on the right
    float x = t.w - 16;
    const Rect meter { x - 120, cy - 9, 120, 18 };
    for (int c = 0; c < 2; ++c) {
        const float v = c == 0 ? meterL_ : meterR_;
        const float db = 20.0f * std::log10(std::max(v, 1e-5f));
        const float n = std::clamp((db + 60.0f) / 60.0f, 0.0f, 1.0f);
        const Rect m { meter.x, meter.y + c * 10.0f, meter.w, 7 };
        beginPath();
        roundedRect(m.x, m.y, m.w, m.h, 3.5f);
        fillColor(theme::well());
        fill();
        if (n > 0) {
            beginPath();
            roundedRect(m.x, m.y, std::max(7.0f, m.w * n), m.h, 3.5f);
            fillColor(v >= 0.99f ? Color(240, 90, 80) : theme::accent());
            fill();
        }
    }
    x = meter.x - 16;
    iconButton({ x - 36, cy - 16, 36, 32 }, kIconFit, open, false, [this] {
        fitView();
        repaint();
    });
    iconButton({ x - 76, cy - 16, 36, 32 }, kIconPlus, open, false, [this] { zoomAround(0.5, wave_.cx()); });
    iconButton({ x - 116, cy - 16, 36, 32 }, kIconMinus, open, false, [this] { zoomAround(2.0, wave_.cx()); });
}

// ---- tools panel -----------------------------------------------------------

void EditorWindow::drawPanel()
{
    beginPath();
    rect(panel_.x, panel_.y, panel_.w, panel_.h);
    fillColor(Color(21, 23, 28));
    fill();
    beginPath();
    moveTo(panel_.x + 0.5f, panel_.y);
    lineTo(panel_.x + 0.5f, panel_.y + panel_.h);
    strokeColor(theme::panelEdge());
    strokeWidth(1.0f);
    stroke();

    // tabs
    {
        const char* names[] = { "Repair", "Utility" };
        const float tw = (panel_.w - 32) / 2, ty = panel_.y + 9, th = 26;
        beginPath();
        roundedRect(panel_.x + 16, ty, tw * 2, th, th * 0.5f);
        fillColor(Color(30, 33, 39));
        fill();
        strokeColor(theme::panelEdge());
        strokeWidth(1.0f);
        stroke();
        for (int i = 0; i < 2; ++i) {
            const Rect r { panel_.x + 16 + i * tw, ty, tw, th };
            if (panelTab_ == i) {
                beginPath();
                roundedRect(r.x + 2, r.y + 2, r.w - 4, r.h - 4, (th - 4) * 0.5f);
                fillColor(theme::accent());
                fill();
            }
            text(r.cx(), r.cy() + 0.5f, 12.0f, panelTab_ == i ? theme::well() : theme::textDim(),
                 ALIGN_CENTER | ALIGN_MIDDLE, names[i]);
            hits_.push_back({ r, [this, i] {
                                 panelTab_ = i;
                                 repaint();
                             } });
        }
    }
    // the utility tab has no footer: its modules have their own buttons
    const Rect list = panelTab_ == 0 ? panelList_ : Rect { panelList_.x, panelList_.y, panelList_.w, panel_.y + panel_.h - panelList_.y };
    float& scroll = tabScroll_[panelTab_];

    save();
    scissor(list.x, list.y, list.w, list.h);
    const size_t hitsBefore = hits_.size(), knobsBefore = knobHits_.size();
    float y = list.y - scroll;
    if (panelTab_ == 0) {
        for (ToolId id : chainOrder())
            y = drawToolCard(id, y) + 8;
    } else {
        for (ModuleId id : moduleOrder())
            y = drawModuleCard(id, y) + 8;
    }
    panelContentH_ = y + scroll - list.y;
    restore();
    scroll = std::clamp(scroll, 0.0f, std::max(0.0f, panelContentH_ - list.h));
    // clicks only count inside the visible list
    auto clip = [&](Rect& r) {
        const float y0 = std::max(r.y, list.y), y1 = std::min(r.y + r.h, list.y + list.h);
        r.y = y0;
        r.h = std::max(0.0f, y1 - y0);
    };
    for (size_t i = hitsBefore; i < hits_.size(); ++i)
        clip(hits_[i].r);
    for (size_t i = knobsBefore; i < knobHits_.size(); ++i)
        clip(knobHits_[i].r);
    if (panelContentH_ > list.h) { // scroll indicator
        const float frac = list.h / panelContentH_;
        const float sy = list.y + (scroll / panelContentH_) * list.h;
        beginPath();
        roundedRect(panel_.x + panel_.w - 6, sy, 3, list.h * frac, 1.5f);
        fillColor(Color(255, 255, 255, 0.15f));
        fill();
    }
    if (panelTab_ != 0)
        return;

    // footer: render
    const Rect& f = panelFooter_;
    beginPath();
    moveTo(f.x, f.y + 0.5f);
    lineTo(f.x + f.w, f.y + 0.5f);
    strokeColor(theme::panelEdge());
    stroke();
    int enabled = 0;
    for (const auto& st : chain_)
        enabled += st.enabled ? 1 : 0;
    if (renderer_.busy()) {
        button({ f.x + 16, f.y + 16, f.w - 32, 40 }, "Cancel render", true, false, [this] { renderer_.cancel(); });
    } else {
        const std::string label = !doc_.isOpen() ? "Render"
            : boxActive_ && hasSelection()      ? "Render box"
            : hasSelection()                    ? "Render selection"
                                                : "Render whole file";
        button({ f.x + 16, f.y + 16, f.w - 32, 40 }, label.c_str(), doc_.isOpen() && enabled > 0 && !busy(), true,
               [this] { startRender(); });
    }
    text(f.cx(), f.y + 74, 11.0f, theme::textFaint(), ALIGN_CENTER | ALIGN_MIDDLE,
         enabled == 0 ? "Switch on one or more tools above"
                      : fmt("%.0f tool", enabled) + (enabled == 1 ? "" : "s") + " in the chain  \xc2\xb7  undo with Ctrl+Z");
}

float EditorWindow::paramsHeight(const std::vector<ToolParam>& params, const std::vector<float>& values)
{
    int rows = 0, knobs = 0;
    for (const auto& p : params)
        if (paramShown(p, values))
            (p.kind == ToolParam::Toggle || p.kind == ToolParam::Choice ? rows : knobs)++;
    return rows * 36.0f + ((knobs + 2) / 3) * 104.0f;
}

float EditorWindow::drawParams(const std::vector<ToolParam>& params, std::vector<float>& values, float x, float y,
                               float w, int chainTool)
{
    // rows: toggles and choices
    for (size_t pi = 0; pi < params.size(); ++pi) {
        const ToolParam& p = params[pi];
        if (!paramShown(p, values))
            continue;
        if (p.kind == ToolParam::Toggle) {
            Toggle tg;
            tg.bounds = { x, y, w, 30 };
            tg.label = p.label;
            tg.on = values[pi] > 0.5f;
            tg.draw(*this);
            float* v = &values[pi];
            hits_.push_back({ tg.bounds, [this, v, chainTool] {
                                 *v = *v > 0.5f ? 0.0f : 1.0f;
                                 paramChanged(chainTool);
                             } });
            y += 36;
        } else if (p.kind == ToolParam::Choice) {
            const Rect r { x, y, w, 30 };
            beginPath();
            roundedRect(r.x, r.y, r.w, r.h, 15);
            fillColor(theme::well());
            fill();
            strokeColor(theme::panelEdge());
            strokeWidth(1.0f);
            stroke();
            text(r.x + 14, r.cy(), 12.0f, theme::textDim(), ALIGN_LEFT | ALIGN_MIDDLE, p.label);
            const int idx = std::clamp(static_cast<int>(std::lround(values[pi])), static_cast<int>(p.min),
                                       static_cast<int>(p.max));
            text(r.x + r.w - 34, r.cy(), 12.5f, theme::text(), ALIGN_RIGHT | ALIGN_MIDDLE, p.choices[idx]);
            { // click the left part to step back, the right part to step forward
                const float cx = r.x + r.w - 18;
                beginPath();
                moveTo(cx - 4, r.cy() - 5);
                lineTo(cx + 2, r.cy());
                lineTo(cx - 4, r.cy() + 5);
                strokeColor(theme::textDim());
                strokeWidth(1.6f);
                stroke();
            }
            float* v = &values[pi];
            const ToolParam* pp = &p;
            if (p.max - p.min >= 6.0f) { // long list: open it
                const int index = static_cast<int>(pi);
                hits_.push_back({ r, [this, v, pp, chainTool, index] { choiceList_ = { v, pp, chainTool, index }; } });
                y += 36;
                continue;
            }
            hits_.push_back({ { r.x, r.y, r.w * 0.4f, r.h }, [this, v, pp, chainTool] {
                                 *v = *v <= pp->min ? pp->max : *v - 1.0f;
                                 paramChanged(chainTool);
                             } });
            hits_.push_back({ { r.x + r.w * 0.4f, r.y, r.w * 0.6f, r.h }, [this, v, pp, chainTool] {
                                 *v = *v >= pp->max ? pp->min : *v + 1.0f;
                                 paramChanged(chainTool);
                             } });
            y += 36;
        }
    }
    // knobs, three to a row
    const float colW = w / 3.0f;
    int k = 0;
    for (size_t pi = 0; pi < params.size(); ++pi) {
        const ToolParam& p = params[pi];
        if (p.kind == ToolParam::Toggle || p.kind == ToolParam::Choice || !paramShown(p, values))
            continue;
        Knob kn;
        kn.bounds = { x + (k % 3) * colW, y + (k / 3) * 104.0f, colW, 100 };
        kn.label = p.label;
        for (auto& ch : kn.label)
            ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        kn.min = p.min;
        kn.max = p.max;
        kn.def = p.def;
        kn.logarithmic = p.kind == ToolParam::LogKnob;
        kn.integer = p.kind == ToolParam::IntKnob;
        kn.value = values[pi];
        // Under automatic control the knob follows the live (detected) value.
        kn.automatic = chainTool >= 0 && knobIsAuto(static_cast<ToolId>(chainTool), static_cast<int>(pi), chain_[chainTool]);
        float shown = kn.value;
        if (kn.automatic && pi < static_cast<size_t>(kLive) && std::isfinite(liveShown_[chainTool][pi]))
            shown = liveShown_[chainTool][pi];
        const char* f = p.format;
        const int offEnd = p.offEnd;
        const float lo = p.min, hi = p.max;
        const char* endText = p.endText ? p.endText : "Off";
        kn.format = [f, offEnd, lo, hi, endText](float v) {
            if ((offEnd < 0 && v <= lo * 1.0001f) || (offEnd > 0 && v >= hi * 0.9999f))
                return std::string(endText);
            return fmt(f, v);
        };
        kn.draw(*this, shown);
        knobHits_.push_back({ kn.bounds, &values[pi], &p, chainTool, static_cast<int>(pi) });
        ++k;
    }
    return y + ((k + 2) / 3) * 104.0f;
}

void EditorWindow::paramChanged(int chainTool)
{
    if (chainTool >= 0)
        settingsChanged(false);
    else
        repaint();
}

float EditorWindow::drawToolCard(ToolId id, float y)
{
    const int ti = static_cast<int>(id);
    ToolSettings& s = chain_[ti];
    const auto& params = toolParams(id);
    const float x = panel_.x + 12, w = panel_.w - 24;
    const bool open = expanded_[ti];
    const bool learnRow = id == ToolId::DeNoise && !(s.values.size() > 4 && s.values[4] > 0.5f); // any-sound mode
    const float bodyH = open ? 8 + paramsHeight(params, s.values) + metersHeight(id) + (learnRow ? 46.0f : 0.0f) + 4 : 0;
    const float h = 56 + bodyH;

    beginPath();
    roundedRect(x, y, w, h, 10);
    fillColor(theme::panel());
    fill();
    strokeColor(s.enabled ? Color(64, 214, 196, 0.5f) : theme::panelEdge());
    strokeWidth(1.0f);
    stroke();

    // header: enable switch, name, chevron
    const Rect sw { x + 14, y + 18, 34, 20 };
    beginPath();
    roundedRect(sw.x, sw.y, sw.w, sw.h, 10);
    fillColor(s.enabled ? theme::accent() : theme::knobTrack());
    fill();
    beginPath();
    circle(s.enabled ? sw.x + sw.w - 10 : sw.x + 10, sw.cy(), 7);
    fillColor(s.enabled ? theme::well() : theme::textDim());
    fill();
    hits_.push_back({ { sw.x - 6, y, sw.w + 12, 56 }, [this, ti] {
                         chain_[ti].enabled = !chain_[ti].enabled;
                         settingsChanged(true);
                     } });
    text(x + 62, y + 21, 14.5f, s.enabled ? theme::text() : theme::textDim(), ALIGN_LEFT | ALIGN_MIDDLE, toolName(id));
    text(x + 62, y + 39, 11.0f, theme::textFaint(), ALIGN_LEFT | ALIGN_MIDDLE, toolBlurb(id));
    {
        const float cx = x + w - 22, cy = y + 28;
        beginPath();
        moveTo(cx - 5, open ? cy + 3 : cy - 3);
        lineTo(cx, open ? cy - 3 : cy + 3);
        lineTo(cx + 5, open ? cy + 3 : cy - 3);
        strokeColor(theme::textDim());
        strokeWidth(1.8f);
        stroke();
        hits_.push_back({ { sw.x + sw.w + 8, y, w - (sw.w + 30), 56 }, [this, ti] {
                             expanded_[ti] = !expanded_[ti];
                             repaint();
                         } });
    }
    if (!open)
        return y + h;

    float by = drawParams(params, s.values, x + 12, y + 60, w - 24, ti);
    by = drawMeters(id, x + 12, by, w - 24);
    if (learnRow) {
        const bool canLearn = doc_.isOpen() && hasSelection() && !busy();
        button({ x + 12, by + 4, 190, 32 }, "Learn from selection", canLearn, false, [this] { learnNoise(); });
        const bool has = !s.noiseProfile.empty();
        text(x + 214, by + 20, 11.5f, has ? theme::accent() : theme::textFaint(), ALIGN_LEFT | ALIGN_MIDDLE,
             has ? (s.values[0] > 0.5f ? "Profile ready" : "Using profile") : "No profile");
    }
    return y + h;
}

float EditorWindow::drawModuleCard(ModuleId id, float y)
{
    const int mi = static_cast<int>(id);
    ModuleState& st = modules_[mi];
    const ModuleInfo& info = moduleInfo(id);
    const float x = panel_.x + 12, w = panel_.w - 24;
    const bool open = st.expanded;
    const bool hasButtons = info.primary != nullptr || info.secondary != nullptr;
    std::vector<std::string> lines = st.info;
    if (id == ModuleId::EqMatch)
        lines.insert(lines.begin(), eqReference_.empty() ? std::string("No reference yet") : eqReferenceInfo_);
    if (id == ModuleId::SpectralRepair)
        lines.insert(lines.begin(), boxActive_ && hasSelection() ? fmt("Box: %.0f", boxLoHz_) + fmt(" to %.0f Hz", boxHiHz_)
                                                                 : std::string("Tip: draw a box with the Box select tool"));
    const bool plot = id == ModuleId::Stats && !st.spectrumDb.empty();
    const float bodyH = open ? 8 + paramsHeight(info.params, {}) + lines.size() * 17.0f + (lines.empty() ? 0 : 8)
            + (plot ? 118.0f : 0.0f) + (hasButtons ? 44.0f : 0.0f) + 4
                             : 0;
    const float h = 50 + bodyH;

    beginPath();
    roundedRect(x, y, w, h, 10);
    fillColor(theme::panel());
    fill();
    strokeColor(theme::panelEdge());
    strokeWidth(1.0f);
    stroke();
    text(x + 16, y + 18, 14.0f, theme::text(), ALIGN_LEFT | ALIGN_MIDDLE, info.name);
    text(x + 16, y + 35, 11.0f, theme::textFaint(), ALIGN_LEFT | ALIGN_MIDDLE, info.blurb);
    {
        const float cx = x + w - 22, cy = y + 25;
        beginPath();
        moveTo(cx - 5, open ? cy + 3 : cy - 3);
        lineTo(cx, open ? cy - 3 : cy + 3);
        lineTo(cx + 5, open ? cy + 3 : cy - 3);
        strokeColor(theme::textDim());
        strokeWidth(1.8f);
        stroke();
        hits_.push_back({ { x, y, w, 50 }, [this, mi] {
                             modules_[mi].expanded = !modules_[mi].expanded;
                             repaint();
                         } });
    }
    if (!open)
        return y + h;

    float by = drawParams(info.params, st.values, x + 12, y + 56, w - 24, -1);
    if (!lines.empty()) {
        by += 4;
        for (const auto& l : lines) {
            text(x + 16, by + 8, 11.5f, theme::textDim(), ALIGN_LEFT | ALIGN_MIDDLE, l);
            by += 17;
        }
        by += 4;
    }
    if (plot) { // average spectrum, log frequency, -100..0 dB
        const Rect g { x + 12, by + 4, w - 24, 106 };
        beginPath();
        roundedRect(g.x, g.y, g.w, g.h, 6);
        fillColor(theme::well());
        fill();
        const double nyq = st.spectrumBinHz * (st.spectrumDb.size() - 1);
        const auto xOf = [&](double hz) { return g.x + 4 + static_cast<float>(std::log(hz / 20.0) / std::log(nyq / 20.0)) * (g.w - 8); };
        for (double hz : { 100.0, 1000.0, 10000.0 }) {
            if (hz >= nyq)
                continue;
            beginPath();
            moveTo(xOf(hz), g.y + 2);
            lineTo(xOf(hz), g.y + g.h - 2);
            strokeColor(theme::grid());
            strokeWidth(1.0f);
            stroke();
            text(xOf(hz) + 3, g.y + g.h - 8, 9.0f, theme::textFaint(), ALIGN_LEFT | ALIGN_MIDDLE,
                 hz >= 1000 ? fmt("%.0fk", hz / 1000) : fmt("%.0f", hz));
        }
        beginPath();
        bool first = true;
        for (int px = 0; px <= static_cast<int>(g.w - 8); ++px) {
            const double hz = 20.0 * std::pow(nyq / 20.0, px / (g.w - 8));
            const double kf = hz / st.spectrumBinHz;
            const size_t k = std::min(st.spectrumDb.size() - 1, static_cast<size_t>(kf));
            const double d = std::clamp(st.spectrumDb[k], -100.0, 0.0);
            const float yy = g.y + 4 + static_cast<float>(-d / 100.0) * (g.h - 8);
            if (first)
                moveTo(g.x + 4 + px, yy);
            else
                lineTo(g.x + 4 + px, yy);
            first = false;
        }
        strokeColor(theme::accent());
        strokeWidth(1.5f);
        stroke();
        by += 118;
    }
    if (hasButtons) {
        const bool stereoOk = !info.stereoOnly || doc_.audio().numChannels() >= 2;
        const bool selOk = !info.needsSelection || hasSelection();
        const bool can = doc_.isOpen() && !busy() && stereoOk;
        float bx = x + 12;
        if (info.primary != nullptr) {
            const float bw = info.secondary ? (w - 24 - 8) * 0.5f : w - 24;
            button({ bx, by + 6, bw, 32 }, info.primary, can && selOk, true, [this, id] { runModuleAction(id, 0); });
            bx += bw + 8;
        }
        if (info.secondary != nullptr) {
            const float bw = (w - 24 - 8) * 0.5f;
            // learning a reference or measuring is fine without a selection, except Learn
            const bool secOk = id == ModuleId::EqMatch ? hasSelection() : true;
            button({ bx, by + 6, bw, 32 }, info.secondary, can && secOk, false, [this, id] { runModuleAction(id, 1); });
        }
    }
    return y + h;
}

// ---- events --------------------------------------------------------------

bool EditorWindow::onMouse(const MouseEvent& ev)
{
    const float s = static_cast<float>(scale());
    const float mx = static_cast<float>(ev.pos.getX()) / s, my = static_cast<float>(ev.pos.getY()) / s;

    if (!ev.press) {
        if ((drag_ == Drag::Select || drag_ == Drag::Box) && hasSelection()) {
            // a click without a real drag just places the cursor
            if (std::abs(xAtFrame(static_cast<double>(selEnd_)) - xAtFrame(static_cast<double>(selStart_))) < 3.0f)
                selStart_ = selEnd_ = 0;
        }
        if (drag_ == Drag::Box && (!hasSelection() || boxHiHz_ <= boxLoHz_))
            boxActive_ = false;
        drag_ = Drag::None;
        repaint();
        return true;
    }
    if (ev.button != kMouseButtonLeft)
        return false;

    if (audioPanel_.isOpen() && !prompt_.active) {
        audioPanel_.mouseDown(mx, my);
        repaint();
        return true;
    }
    if (prompt_.active) { // a question is open: only its buttons work
        for (size_t i = 0; i < promptButtons_.size(); ++i)
            if (promptButtons_[i].contains(mx, my)) {
                answerPrompt(static_cast<int>(i));
                break;
            }
        return true;
    }
    if (fileJob_ != nullptr) { // opening or saving: only Cancel works
        for (const auto& h : hits_)
            if (h.r.h > 0 && h.r.contains(mx, my)) {
                h.click();
                break;
            }
        return true;
    }

    // an open choice list takes the click
    if (choiceList_.value != nullptr) {
        for (const auto& [r, index] : choiceRects_) {
            if (!r.contains(mx, my))
                continue;
            *choiceList_.value = static_cast<float>(index);
            // Picking a microphone in Voice also sets its starting controls.
            if (choiceList_.chainTool == static_cast<int>(ToolId::Voice) && choiceList_.paramIndex == 0) {
                const auto& m = mend::VoiceClean::mic(index);
                auto& v = chain_[choiceList_.chainTool].values;
                v[2] = m.clarity;
                v[3] = m.smooth;
                v[4] = m.deMud;
            }
            paramChanged(choiceList_.chainTool);
            break;
        }
        choiceList_ = {};
        repaint();
        return true;
    }

    // view blend slider (click or drag; snaps to the ends and the middle)
    if (doc_.isOpen() && blendTrack_.w > 0
        && Rect { blendTrack_.x - 10, blendTrack_.y, blendTrack_.w + 20, blendTrack_.h }.contains(mx, my)) {
        drag_ = Drag::ViewBlend;
        setViewBlend(mx);
        return true;
    }

    // knobs (drag; double-click resets)
    for (const auto& kh : knobHits_) {
        if (kh.r.h <= 0 || !kh.r.contains(mx, my))
            continue;
        const auto now = std::chrono::steady_clock::now();
        const bool dbl = kh.value == lastKnobValue_ && now - lastKnobClick_ < std::chrono::milliseconds(350);
        lastKnobClick_ = now;
        lastKnobValue_ = kh.value;
        // grabbing an automatic knob switches that tool to manual
        if (kh.chainTool >= 0 && knobIsAuto(static_cast<ToolId>(kh.chainTool), kh.index, chain_[kh.chainTool])) {
            chain_[kh.chainTool].values[0] = 0.0f;
            const float live = kh.index < kLive ? liveShown_[kh.chainTool][kh.index] : NAN;
            if (std::isfinite(live)) // carry on from where the detection was
                *kh.value = std::clamp(live, kh.param->min, kh.param->max);
        }
        if (dbl) {
            *kh.value = kh.param->def;
            paramChanged(kh.chainTool);
            return true;
        }
        drag_ = Drag::Knob;
        dragKnob_ = kh;
        dragStartY_ = my;
        dragStartNorm_ = toNorm(*kh.param, *kh.value);
        return true;
    }
    for (const auto& h : hits_) {
        if (h.r.h > 0 && h.r.contains(mx, my)) {
            h.click();
            repaint();
            return true;
        }
    }
    if (!doc_.isOpen())
        return false;

    if (overview_.contains(mx, my)) {
        drag_ = Drag::Overview;
        const double fppAll = static_cast<double>(numFrames()) / overview_.w;
        viewStart_ = (mx - overview_.x) * fppAll - wave_.w * framesPerPx_ * 0.5;
        clampView();
        repaint();
        return true;
    }
    float lanePos = 0.0f;
    if (selectTool_ == SelectTool::Box && spectrumShown() && wave_.contains(mx, my) && laneAt(my, lanePos) >= 0) {
        const double f = std::clamp(frameAtX(mx), 0.0, static_cast<double>(numFrames()));
        boxAnchorHz_ = mend::editor::specFreqAt(freqScale_, doc_.audio().sampleRate, lanePos);
        boxLoHz_ = boxHiHz_ = boxAnchorHz_;
        boxActive_ = true;
        cursor_ = static_cast<size_t>(f);
        selStart_ = selEnd_ = cursor_;
        dragAnchor_ = f;
        drag_ = Drag::Box;
        repaint();
        return true;
    }
    if (wave_.contains(mx, my) || ruler_.contains(mx, my)) {
        const double f = std::clamp(frameAtX(mx), 0.0, static_cast<double>(numFrames()));
        // grab a selection edge if close to one (a box keeps its band)
        if (hasSelection() && std::abs(mx - xAtFrame(static_cast<double>(selStart_))) < 5) {
            drag_ = Drag::SelStartEdge;
        } else if (hasSelection() && std::abs(mx - xAtFrame(static_cast<double>(selEnd_))) < 5) {
            drag_ = Drag::SelEndEdge;
        } else if (ev.mod & kModifierShift) {
            boxActive_ = false;
            const size_t ff = static_cast<size_t>(f);
            const size_t anchor = hasSelection() ? selStart_ : cursor_;
            selStart_ = std::min(anchor, ff);
            selEnd_ = std::max(anchor, ff);
            dragAnchor_ = static_cast<double>(anchor);
            drag_ = Drag::Select;
        } else {
            boxActive_ = false;
            cursor_ = static_cast<size_t>(f);
            selStart_ = selEnd_ = cursor_;
            dragAnchor_ = f;
            drag_ = Drag::Select;
            if (player_.playing())
                togglePlay(), togglePlay(); // restart from the new cursor
        }
        repaint();
        return true;
    }
    return false;
}

bool EditorWindow::onMotion(const MotionEvent& ev)
{
    if (audioPanel_.isOpen()) {
        const float sc = static_cast<float>(scale());
        audioPanel_.motion(ev.pos.getX() / sc, ev.pos.getY() / sc);
        repaint();
        return true;
    }
    if (drag_ == Drag::None)
        return false;
    const float s = static_cast<float>(scale());
    const float mx = static_cast<float>(ev.pos.getX()) / s, my = static_cast<float>(ev.pos.getY()) / s;
    if (drag_ == Drag::ViewBlend) {
        setViewBlend(mx);
        return true;
    }
    if (drag_ == Drag::Knob) {
        const double sens = (ev.mod & kModifierShift) ? 1.0 / 1000.0 : 1.0 / 200.0;
        const float v = fromNorm(*dragKnob_.param, static_cast<float>(dragStartNorm_ + (dragStartY_ - my) * sens));
        if (v != *dragKnob_.value) {
            *dragKnob_.value = v;
            paramChanged(dragKnob_.chainTool);
        }
        return true;
    }
    if (drag_ == Drag::Overview) {
        const double fppAll = static_cast<double>(numFrames()) / overview_.w;
        viewStart_ = (mx - overview_.x) * fppAll - wave_.w * framesPerPx_ * 0.5;
        clampView();
        repaint();
        return true;
    }
    const double f = std::clamp(frameAtX(mx), 0.0, static_cast<double>(numFrames()));
    if (drag_ == Drag::Box) {
        selStart_ = static_cast<size_t>(std::min(dragAnchor_, f));
        selEnd_ = static_cast<size_t>(std::max(dragAnchor_, f));
        int wx, wy, ww, laneH, gap;
        laneGeometry(doc_.audio().numChannels(), wx, wy, ww, laneH, gap);
        const double sc = scale();
        // position within whichever lane the drag started in, clamped
        float pos = 0.0f;
        const int lane = laneAt(my, pos);
        if (lane < 0) {
            const float ly0 = static_cast<float>(wy / sc), lh = static_cast<float>(laneH / sc);
            pos = std::clamp((my - ly0) / lh, 0.0f, 1.0f);
        }
        const double hz = mend::editor::specFreqAt(freqScale_, doc_.audio().sampleRate, pos);
        boxLoHz_ = std::min(boxAnchorHz_, hz);
        boxHiHz_ = std::max(boxAnchorHz_, hz);
    } else if (drag_ == Drag::Select) {
        selStart_ = static_cast<size_t>(std::min(dragAnchor_, f));
        selEnd_ = static_cast<size_t>(std::max(dragAnchor_, f));
    } else if (drag_ == Drag::SelStartEdge) {
        selStart_ = std::min(static_cast<size_t>(f), selEnd_);
    } else if (drag_ == Drag::SelEndEdge) {
        selEnd_ = std::max(static_cast<size_t>(f), selStart_);
    }
    // scroll while dragging past the edges
    if (mx > wave_.x + wave_.w)
        viewStart_ += (mx - (wave_.x + wave_.w)) * framesPerPx_;
    else if (mx < wave_.x)
        viewStart_ -= (wave_.x - mx) * framesPerPx_;
    clampView();
    repaint();
    return true;
}

bool EditorWindow::onScroll(const ScrollEvent& ev)
{
    const float s = static_cast<float>(scale());
    const float mx = static_cast<float>(ev.pos.getX()) / s, my = static_cast<float>(ev.pos.getY()) / s;
    const float dy = static_cast<float>(ev.delta.getY()), dx = static_cast<float>(ev.delta.getX());
    if (audioPanel_.isOpen()) {
        audioPanel_.scroll(mx, my, dy);
        repaint();
        return true;
    }
    if (panel_.contains(mx, my)) {
        float& sc = tabScroll_[panelTab_];
        sc = std::max(0.0f, sc - dy * 40.0f); // clamped when drawn
        repaint();
        return true;
    }
    if (!doc_.isOpen())
        return false;
    if (wave_.contains(mx, my) || overview_.contains(mx, my) || ruler_.contains(mx, my)) {
        if ((ev.mod & kModifierShift) || std::abs(dx) > std::abs(dy)) {
            const float d = std::abs(dx) > std::abs(dy) ? dx : dy;
            viewStart_ -= d * 60.0 * framesPerPx_;
            clampView();
            repaint();
        } else {
            zoomAround(dy > 0 ? 0.8 : 1.25, mx);
        }
        return true;
    }
    return false;
}

bool EditorWindow::onKeyboard(const KeyboardEvent& ev)
{
    if (!ev.press)
        return false;
    if (prompt_.active) { // Enter: the default answer, Escape: cancel
        if (ev.key == kKeyEnter || ev.key == '\r' || ev.key == '\n')
            answerPrompt(0);
        else if (ev.key == kKeyEscape)
            answerPrompt(static_cast<int>(prompt_.buttons.size()) - 1);
        return true;
    }
    if (audioPanel_.isOpen()) { // Escape closes it; other keys wait
        if (ev.key == kKeyEscape)
            audioPanel_.escape();
        repaint();
        return true;
    }
    const bool ctrl = (ev.mod & kModifierControl) != 0;
    const bool shift = (ev.mod & kModifierShift) != 0;
    uint k = ev.key;
    if (ctrl && k >= 1 && k <= 26)
        k += 'a' - 1; // some platforms deliver control characters
    if (k >= 'A' && k <= 'Z')
        k += 'a' - 'A';

    if (ctrl) {
        switch (k) {
        case 'o': chooseOpen(); return true;
        case 's': chooseSaveProject(shift); return true;
        case 'e': chooseExport(); return true;
        case 'z': shift ? redo() : undo(); return true;
        case 'y': redo(); return true;
        case 'a':
            if (doc_.isOpen()) {
                boxActive_ = false;
                selStart_ = 0;
                selEnd_ = numFrames();
                repaint();
            }
            return true;
        case 'r': startRender(); return true;
        }
        return false;
    }
    switch (k) {
    case kKeySpace: togglePlay(); return true;
    case kKeyEscape:
        if (busy()) {
            renderer_.cancel();
            jobs_.cancel();
            return true;
        }
        selStart_ = selEnd_ = 0;
        boxActive_ = false;
        repaint();
        return true;
    case 'b':
        selectTool_ = SelectTool::Box;
        if (!showSpectrogram())
            viewBlend_ = 0.5f;
        repaint();
        return true;
    case 't':
        selectTool_ = SelectTool::Time;
        repaint();
        return true;
    case kKeyHome:
        cursor_ = 0;
        viewStart_ = 0;
        repaint();
        return true;
    case '=':
    case '+': zoomAround(0.5, wave_.cx()); return true;
    case '-': zoomAround(2.0, wave_.cx()); return true;
    case 'f':
        fitView();
        repaint();
        return true;
    }
    return false;
}

// ---- actions ---------------------------------------------------------------

void EditorWindow::setStatus(const std::string& s, bool error)
{
    status_ = s;
    statusError_ = error;
    repaint();
}

void EditorWindow::chooseOpen()
{
    if (busy())
        return;
    if (dirty()) {
        askToSave(After::OpenDialog);
        return;
    }
    openDialogNow();
}

// Where file choosers open: the last folder used, else the open file's.
std::string EditorWindow::startFolder() const
{
    if (!lastDir_.empty())
        return lastDir_;
    const std::string& p = projectPath_.empty() ? doc_.path() : projectPath_;
    const size_t slash = p.find_last_of("/\\");
    return slash == std::string::npos ? std::string() : p.substr(0, slash);
}

void EditorWindow::openDialogNow()
{
    FileBrowserOptions opts;
    opts.title = "Open audio file or Mend project";
    opts.startDir = lastDir_.empty() ? nullptr : lastDir_.c_str();
    browsing_ = Browse::Open;
    if (!openFileBrowser(opts)) {
        browsing_ = Browse::None;
        setStatus("Couldn't open the file chooser.", true);
    }
}

void EditorWindow::chooseExport()
{
    if (!doc_.isOpen() || busy())
        return;
    FileBrowserOptions opts;
    opts.saving = true;
    opts.title = "Export WAV";
    std::string base = doc_.displayName();
    const size_t dot = base.find_last_of('.');
    if (dot != std::string::npos)
        base = base.substr(0, dot);
    static std::string defaultName;
    defaultName = base + "-mend.wav";
    opts.defaultName = defaultName.c_str();
    static std::string dir;
    dir = startFolder();
    opts.startDir = dir.empty() ? nullptr : dir.c_str();
    browsing_ = Browse::Export;
    if (!openFileBrowser(opts)) {
        browsing_ = Browse::None;
        setStatus("Couldn't open the file chooser.", true);
    }
}

void EditorWindow::onFileSelected(const char* filename)
{
    const Browse mode = browsing_;
    browsing_ = Browse::None;
    if (filename == nullptr || filename[0] == '\0') {
        after_ = After::None; // a save chooser was cancelled: don't close or open
        return;
    }
    std::string path(filename);
    const size_t slash = path.find_last_of("/\\");
    if (slash != std::string::npos)
        lastDir_ = path.substr(0, slash);
    if (mode == Browse::Open)
        openPath(path);
    else if (mode == Browse::Export)
        doExport(path);
    else if (mode == Browse::SaveProject) {
        if (!isProjectFile(path))
            path += kProjectExtension;
        startSave(path);
    }
}

void EditorWindow::openPath(const std::string& path)
{
    if (busy())
        return;
    if (dirty()) {
        askToSave(After::OpenPath, path);
        return;
    }
    startLoad(path);
}

// ---- opening and saving (background) ----------------------------------------

void EditorWindow::startLoad(const std::string& path)
{
    player_.stop();
    renderer_.cancel();
    jobs_.cancel();
    jobs_.wait();
    {   // drop any finished job result for the old file
        ModuleId id;
        int action;
        bool ok;
        ModuleOutput o;
        std::string e;
        jobs_.take(id, action, ok, o, e);
    }
    fileJob_ = std::make_unique<FileJob>();
    FileJob* job = fileJob_.get();
    job->kind = FileJob::Load;
    job->path = path;
    job->isProject = isProjectFile(path);
    job->thread = std::thread([job] {
        try {
            job->ok = job->isProject
                ? loadProject(job->path, job->audio, job->project, job->error, &job->progress, &job->cancel)
                : loadAudioFile(job->path, job->audio, job->error, &job->progress, &job->cancel);
        } catch (const std::bad_alloc&) {
            job->error = "Not enough memory to open this file.";
            job->ok = false;
        } catch (const std::exception& e) {
            job->error = std::string("Couldn't open the file: ") + e.what();
            job->ok = false;
        }
        job->done.store(true);
    });
    repaint();
}

void EditorWindow::startSave(const std::string& path)
{
    if (!doc_.isOpen() || fileJob_ != nullptr)
        return;
    player_.stop();
    fileJob_ = std::make_unique<FileJob>();
    FileJob* job = fileJob_.get();
    job->kind = FileJob::Save;
    job->path = path;
    job->isProject = true;
    job->project = captureState();
    const AudioBuffer* audio = &doc_.audio(); // unchanged while the save runs (the editor is busy)
    job->thread = std::thread([job, audio] {
        try {
            job->ok = saveProject(job->path, *audio, job->project, job->error, &job->progress);
        } catch (const std::exception& e) {
            job->error = std::string("Couldn't save the project: ") + e.what();
            job->ok = false;
        }
        job->done.store(true);
    });
    repaint();
}

void EditorWindow::finishFileJob()
{
    if (fileJob_ == nullptr || !fileJob_->done.load())
        return;
    std::unique_ptr<FileJob> job = std::move(fileJob_);
    if (job->thread.joinable())
        job->thread.join();
    const size_t slash = job->path.find_last_of("/\\");
    const std::string name = slash == std::string::npos ? job->path : job->path.substr(slash + 1);

    if (job->kind == FileJob::Save) {
        if (job->ok) {
            projectPath_ = job->path;
            savedVersion_ = doc_.version();
            savedSettings_ = settingsSignature();
            doc_.markSaved();
            setStatus("Saved project " + name);
            runAfter();
        } else {
            after_ = After::None;
            setStatus(job->error, true);
        }
        repaint();
        return;
    }

    if (!job->ok) {
        setStatus(job->cancel.load() ? std::string("Opening cancelled.") : job->error, !job->cancel.load());
        repaint();
        return;
    }
    boxActive_ = false;
    for (auto& m : modules_) {
        m.info.clear();
        m.spectrumDb.clear();
    }
    spec_.quiesce();
    specRequested_ = {};
    specShown_ = {};
    specImg_.clear();
    waveKey_ = {};
    overviewVersion_ = ~uint64_t(0);
    doc_.adopt(std::move(job->audio), job->isProject ? job->project.sourcePath : job->path);
    peaksVersion_ = 0;
    layout();
    fitView();
    cursor_ = 0;
    selStart_ = selEnd_ = 0;
    if (job->isProject) {
        applyState(job->project);
        projectPath_ = job->path;
    } else {
        projectPath_.clear();
    }
    savedVersion_ = doc_.version();
    savedSettings_ = settingsSignature();
    const auto& a = doc_.audio();
    setStatus((job->isProject ? "Opened project " + name : "Opened " + name) + "  ("
              + timeString(static_cast<double>(a.numFrames()), false) + ")");
    repaint();
}

void EditorWindow::drawFileJobOverlay()
{
    if (fileJob_ == nullptr)
        return;
    const float W = static_cast<float>(Window::getWidth() / scale()), H = static_cast<float>(Window::getHeight() / scale());
    beginPath();
    rect(0, 0, W, H);
    fillColor(Color(0, 0, 0, 0.35f));
    fill();
    const size_t slash = fileJob_->path.find_last_of("/\\");
    const std::string name = slash == std::string::npos ? fileJob_->path : fileJob_->path.substr(slash + 1);
    const bool saving = fileJob_->kind == FileJob::Save;
    const float prog = fileJob_->progress.load();
    const Rect b { W * 0.5f - 210, H * 0.5f - 46, 420, 92 };
    mend::ui::drawPanel(*this, b);
    text(b.x + 18, b.y + 24, 13.5f, theme::text(), ALIGN_LEFT | ALIGN_MIDDLE, (saving ? "Saving " : "Opening ") + name);
    if (prog >= 0.0f)
        text(b.x + b.w - 18, b.y + 24, 13.0f, theme::textDim(), ALIGN_RIGHT | ALIGN_MIDDLE, fmt("%.0f%%", prog * 100.0f));
    const Rect bar { b.x + 18, b.y + 50, b.w - (saving ? 36 : 124), 8 };
    beginPath();
    roundedRect(bar.x, bar.y, bar.w, bar.h, 4);
    fillColor(theme::well());
    fill();
    beginPath();
    if (prog >= 0.0f) {
        roundedRect(bar.x, bar.y, std::max(8.0f, bar.w * prog), bar.h, 4);
    } else { // length unknown: a moving block
        const double t = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        const float pos = static_cast<float>(0.5 + 0.5 * std::sin(t * 3.0));
        roundedRect(bar.x + (bar.w - 80) * pos, bar.y, 80, bar.h, 4);
    }
    fillColor(theme::accent());
    fill();
    if (!saving)
        button({ b.x + b.w - 96, b.y + 40, 78, 28 }, "Cancel", true, false, [this] {
            if (fileJob_ != nullptr)
                fileJob_->cancel.store(true);
        });
}

// ---- projects and unsaved changes ---------------------------------------------

ProjectState EditorWindow::captureState() const
{
    ProjectState st;
    st.sourcePath = doc_.path();
    st.chain = chain_;
    for (const auto& m : modules_)
        st.modules.push_back(m.values);
    st.viewBlend = viewBlend_;
    st.freqScale = static_cast<int>(freqScale_);
    st.floorDb = specFloorDb_;
    st.viewStart = viewStart_;
    st.framesPerPx = framesPerPx_;
    st.cursor = cursor_;
    st.selStart = selStart_;
    st.selEnd = selEnd_;
    st.boxActive = boxActive_;
    st.boxLoHz = boxLoHz_;
    st.boxHiHz = boxHiHz_;
    st.preview = previewOn_;
    return st;
}

void EditorWindow::applyState(const ProjectState& st)
{
    if (st.chain.size() == chain_.size())
        chain_ = st.chain;
    for (size_t m = 0; m < modules_.size() && m < st.modules.size(); ++m)
        modules_[m].values = st.modules[m];
    viewBlend_ = st.viewBlend;
    if (st.freqScale >= 0)
        freqScale_ = static_cast<mend::editor::FreqScale>(std::clamp(st.freqScale, 0, 2));
    specFloorDb_ = st.floorDb;
    const size_t n = numFrames();
    cursor_ = std::min(st.cursor, n);
    selStart_ = std::min(st.selStart, n);
    selEnd_ = std::min(std::max(st.selEnd, selStart_), n);
    boxActive_ = st.boxActive && hasSelection();
    boxLoHz_ = st.boxLoHz;
    boxHiHz_ = st.boxHiHz;
    previewOn_ = st.preview;
    if (st.framesPerPx > 0.0) {
        framesPerPx_ = st.framesPerPx;
        viewStart_ = st.viewStart;
        clampView();
    }
}

std::string EditorWindow::settingsSignature() const
{
    std::string sig;
    for (const auto& t : chain_) {
        sig += t.enabled ? '1' : '0';
        for (float v : t.values)
            sig += fmt("%g,", v);
        sig += t.noiseProfile.size() > 0 ? std::to_string(std::hash<std::string>()(t.noiseProfile)) : std::string();
        sig += ';';
    }
    for (const auto& m : modules_)
        for (float v : m.values)
            sig += fmt("%g,", v);
    return sig;
}

// Unsaved work: edits to the audio, or (for a saved project) changed settings.
bool EditorWindow::dirty() const
{
    if (!doc_.isOpen())
        return false;
    if (doc_.version() != savedVersion_)
        return true;
    return !projectPath_.empty() && settingsSignature() != savedSettings_;
}

void EditorWindow::askToSave(After then, const std::string& path)
{
    prompt_.active = true;
    prompt_.title = "Save changes to " + doc_.displayName() + "?";
    prompt_.message = projectPath_.empty() ? "Save them as a Mend project to carry on later, or they will be lost."
                                           : "Your changes will be lost if you don't save them.";
    prompt_.buttons = { "Save project", "Don't save", "Cancel" };
    prompt_.choose = [this, then, path](int choice) {
        if (choice == 2)
            return;
        after_ = then;
        afterPath_ = path;
        if (choice == 0)
            chooseSaveProject(false); // runs after_ once saved
        else
            runAfter();
    };
    repaint();
}

void EditorWindow::runAfter()
{
    const After then = after_;
    after_ = After::None;
    switch (then) {
    case After::Close:
        savedVersion_ = doc_.version(); // nothing left to ask about
        projectPath_.clear();
        close();
        break;
    case After::OpenDialog:
        openDialogNow();
        break;
    case After::OpenPath:
        startLoad(afterPath_);
        break;
    case After::None:
        break;
    }
}

void EditorWindow::chooseSaveProject(bool saveAs)
{
    if (!doc_.isOpen() || busy())
        return;
    if (!saveAs && !projectPath_.empty()) {
        startSave(projectPath_);
        return;
    }
    FileBrowserOptions opts;
    opts.saving = true;
    opts.title = "Save Mend project";
    std::string base = doc_.displayName();
    const size_t dot = base.find_last_of('.');
    if (dot != std::string::npos)
        base = base.substr(0, dot);
    static std::string defaultName;
    defaultName = base + kProjectExtension;
    opts.defaultName = defaultName.c_str();
    static std::string dir;
    dir = startFolder();
    opts.startDir = dir.empty() ? nullptr : dir.c_str();
    browsing_ = Browse::SaveProject;
    if (!openFileBrowser(opts)) {
        browsing_ = Browse::None;
        after_ = After::None;
        setStatus("Couldn't open the file chooser.", true);
    }
}

bool EditorWindow::onClose()
{
    if (fileJob_ != nullptr) {
        if (fileJob_->kind == FileJob::Load)
            fileJob_->cancel.store(true);
        setStatus(fileJob_->kind == FileJob::Save ? "Saving... the window will close when you try again."
                                                  : "Stopping the file that's opening...");
        return false;
    }
    if (dirty()) {
        askToSave(After::Close);
        return false;
    }
    return true;
}

// ---- prompt -------------------------------------------------------------------

void EditorWindow::drawPrompt()
{
    if (!prompt_.active)
        return;
    const float W = static_cast<float>(Window::getWidth() / scale()), H = static_cast<float>(Window::getHeight() / scale());
    beginPath();
    rect(0, 0, W, H);
    fillColor(Color(0, 0, 0, 0.55f));
    fill();
    // Title and message wrap inside the box (a long file name would otherwise
    // run past its edge), and the box grows to fit them.
    const float bw = std::min(520.0f, std::max(300.0f, W - 40.0f));
    const float inner = bw - 44.0f;
    auto wrappedHeight = [&](float size, const std::string& s) {
        if (s.empty())
            return 0.0f;
        fontSize(size);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        float r[4] = {};
        textBoxBounds(0, 0, inner, s.c_str(), nullptr, r);
        return r[3] - r[1];
    };
    const float titleH = wrappedHeight(15.0f, prompt_.title);
    const float msgH = wrappedHeight(12.5f, prompt_.message);
    const float bh = 22.0f + titleH + (msgH > 0.0f ? 10.0f + msgH : 0.0f) + 28.0f + 34.0f + 20.0f;
    const Rect b { std::round(W * 0.5f - bw * 0.5f), std::round(H * 0.5f - bh * 0.5f), bw, bh };
    mend::ui::drawPanel(*this, b);
    fontSize(15.0f);
    fillColor(theme::text());
    textAlign(ALIGN_LEFT | ALIGN_TOP);
    textBox(b.x + 22, b.y + 22, inner, prompt_.title.c_str(), nullptr);
    if (msgH > 0.0f) {
        fontSize(12.5f);
        fillColor(theme::textDim());
        textBox(b.x + 22, b.y + 32 + titleH, inner, prompt_.message.c_str(), nullptr);
    }
    promptButtons_.clear();
    float x = b.x + b.w - 20;
    for (int i = static_cast<int>(prompt_.buttons.size()) - 1; i >= 0; --i) {
        const float w = i == 0 ? 130.0f : 104.0f;
        x -= w;
        promptButtons_.insert(promptButtons_.begin(), Rect { x, b.y + b.h - 54, w, 34 });
        x -= 10;
    }
    for (size_t i = 0; i < prompt_.buttons.size(); ++i) {
        const Rect& r = promptButtons_[i];
        beginPath();
        roundedRect(r.x, r.y, r.w, r.h, 17);
        fillColor(i == 0 ? theme::accent() : Color(38, 42, 50));
        fill();
        text(r.cx(), r.cy() + 0.5f, 12.5f, i == 0 ? theme::well() : theme::text(), ALIGN_CENTER | ALIGN_MIDDLE,
             prompt_.buttons[i]);
    }
}

void EditorWindow::answerPrompt(int choice)
{
    if (!prompt_.active)
        return;
    prompt_.active = false;
    auto choose = std::move(prompt_.choose);
    prompt_.choose = nullptr;
    if (choose)
        choose(choice);
    repaint();
}

void EditorWindow::doExport(const std::string& pathIn)
{
    std::string path = pathIn;
    std::string lower = path;
    for (auto& c : lower)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower.size() < 4 || lower.compare(lower.size() - 4, 4, ".wav") != 0)
        path += ".wav";
    std::string err;
    const auto& fv = modules_[static_cast<int>(ModuleId::ExportFormat)].values;
    const int bits = static_cast<int>(fv[0]), dither = static_cast<int>(fv[1]);
    const WavFormat format = bits == 0 ? WavFormat::Int16 : bits == 1 ? WavFormat::Int24 : WavFormat::Float32;
    static const char* bitNames[] = { "16-bit", "24-bit", "32-bit float" };
    static const char* ditherNames[] = { "", ", TPDF dither", ", noise-shaped dither" };
    if (doc_.exportWav(path, format, dither, err)) {
        doc_.markSaved();
        if (projectPath_.empty()) // the edits are safe in the exported file
            savedVersion_ = doc_.version();
        const size_t slash = path.find_last_of("/\\");
        setStatus("Exported " + (slash == std::string::npos ? path : path.substr(slash + 1)) + " (" + bitNames[bits]
                  + " WAV" + (format == WavFormat::Float32 ? "" : ditherNames[dither]) + ")");
    } else {
        setStatus(err, true);
    }
}

void EditorWindow::setupAudioPanel()
{
    audioPanel_.showInput = false;      // the editor only plays
    audioPanel_.showSampleRate = false; // it plays each file at its own rate
    audioPanel_.footnote = "Shared with the Mend standalone apps.";
    audioPanel_.getState = [this](bool list) {
        mend::ui::AudioPanelState st;
        if (list) {
            const mend::audio::DeviceList d = mend::audio::listDevices();
            st.outputs = d.outputs;
            st.defaultOutput = d.defaultOutput;
            st.backend = d.backend;
        }
        const mend::audio::Settings saved = mend::audio::loadSettings();
        st.output = saved.output;
        st.bufferSize = saved.bufferSize;
        st.running = player_.deviceOpen();
        st.activeOutput = player_.outputName();
        st.activeSampleRate = player_.deviceRate();
        st.activeBufferSize = player_.bufferSize();
        st.latencyMs = player_.latencyMs();
        st.note = player_.deviceOpen() ? player_.note() : std::string();
        if (player_.deviceOpen()) {
            char buf[256];
            std::snprintf(buf, sizeof(buf), "Plays through %s  \xc2\xb7  %u-sample buffer  \xc2\xb7  about %.0f ms",
                          player_.outputName().c_str(), player_.bufferSize(), player_.latencyMs());
            st.status = buf;
        } else {
            st.status = "The output opens when you press Play.";
        }
        return st;
    };
    audioPanel_.apply = [this](const mend::ui::AudioPanelState& st) {
        mend::audio::Settings s = mend::audio::loadSettings(); // keep the standalone apps' input choice
        s.output = st.output;
        s.bufferSize = st.bufferSize;
        mend::audio::saveSettings(s);
        // reopen the output, carrying on from the same place if playing
        const bool was = player_.playing();
        const size_t pos = was ? std::min(player_.position(), numFrames()) : 0;
        player_.reloadSettings();
        if (was && doc_.isOpen()) {
            std::string err;
            if (!player_.play(&doc_.audio(), pos, hasSelection() ? selEnd_ : 0, previewOn_, chain_, err))
                setStatus(err.empty() ? std::string("Playback failed.") : err, true);
        }
    };
}

void EditorWindow::togglePlay()
{
    if (!doc_.isOpen())
        return;
    if (player_.playing()) {
        player_.stop();
        repaint();
        return;
    }
    const size_t from = hasSelection() ? selStart_ : cursor_;
    const size_t to = hasSelection() ? selEnd_ : 0;
    std::string err;
    if (!player_.play(&doc_.audio(), from, to, previewOn_, chain_, err))
        setStatus(err.empty() ? std::string("Playback failed.") : err, true);
    else if (!player_.note().empty() && player_.note() != lastAudioNote_)
        setStatus(player_.note(), true); // e.g. the chosen output isn't connected
    lastAudioNote_ = player_.note();
    repaint();
}

void EditorWindow::stopPlayback()
{
    player_.stop();
    repaint();
}

void EditorWindow::settingsChanged(bool enabledSetChanged)
{
    if (player_.playing()) {
        if (enabledSetChanged) {
            // rebuild the preview chain from where we are
            const size_t pos = std::min(player_.position(), numFrames());
            const size_t to = hasSelection() ? selEnd_ : 0;
            std::string err;
            player_.play(&doc_.audio(), pos, to, previewOn_, chain_, err);
        } else if (player_.previewing()) {
            player_.updateSettings(chain_);
        }
    }
    repaint();
}

void EditorWindow::startRender()
{
    if (!doc_.isOpen() || busy() || !anyEnabled(chain_))
        return;
    player_.stop();
    const size_t s = hasSelection() ? selStart_ : 0;
    const size_t len = hasSelection() ? selEnd_ - selStart_ : numFrames();
    const bool box = boxActive_ && hasSelection();
    renderer_.start(doc_.audio(), s, len, chain_, box ? boxLoHz_ : 0.0, box ? boxHiHz_ : 0.0);
    setStatus("Rendering...");
}

void EditorWindow::collectRender()
{
    size_t start = 0;
    std::vector<std::vector<float>> out;
    std::string label;
    if (!renderer_.takeResult(start, out, label))
        return;
    player_.stop();
    spec_.quiesce();
    doc_.replaceRegion(start, std::move(out), label);
    setStatus("Rendered " + label + ".  Ctrl+Z to undo.");
}

void EditorWindow::undo()
{
    if (busy() || !doc_.canUndo())
        return;
    player_.stop();
    const std::string l = doc_.undoLabel();
    spec_.quiesce();
    const double rate = doc_.audio().sampleRate;
    doc_.undo();
    afterAudioChange(rate);
    setStatus("Undid " + l);
}

void EditorWindow::redo()
{
    if (busy() || !doc_.canRedo())
        return;
    player_.stop();
    const std::string l = doc_.redoLabel();
    spec_.quiesce();
    const double rate = doc_.audio().sampleRate;
    doc_.redo();
    afterAudioChange(rate);
    setStatus("Redid " + l);
}

void EditorWindow::learnNoise()
{
    if (!doc_.isOpen() || !hasSelection())
        return;
    const auto audio = doc_.copyRange(selStart_, selEnd_ - selStart_);
    const std::string p = learnNoiseProfile(audio, doc_.audio().sampleRate);
    if (p.empty()) {
        setStatus("Select at least a quarter of a second of noise to learn from.", true);
        return;
    }
    auto& dn = chain_[static_cast<int>(ToolId::DeNoise)];
    dn.noiseProfile = p;
    dn.values[0] = 0.0f; // use the learned profile rather than adaptive
    dn.enabled = true;
    settingsChanged(true);
    setStatus("Noise profile learned from " + fmt("%.2f s", (selEnd_ - selStart_) / doc_.audio().sampleRate)
              + ". Now select what to clean (or nothing for the whole file) and Render.");
}

void EditorWindow::runModuleAction(ModuleId id, int action)
{
    if (!doc_.isOpen() || busy())
        return;
    const ModuleInfo& info = moduleInfo(id);
    ModuleInput in;
    in.audio = &doc_.audio();
    in.partial = hasSelection() && !info.wholeFile;
    in.start = in.partial ? selStart_ : 0;
    in.length = in.partial ? selEnd_ - selStart_ : numFrames();
    if (boxActive_ && hasSelection()) {
        in.loHz = boxLoHz_;
        in.hiHz = boxHiHz_;
    }
    in.values = modules_[static_cast<int>(id)].values;
    in.eqReference = eqReference_;
    // measuring leaves playback alone; anything that edits stops it
    const bool measuring = action == 1 && (id == ModuleId::Azimuth || id == ModuleId::Loudness || id == ModuleId::Phase
                                           || id == ModuleId::EqMatch);
    if (!measuring && id != ModuleId::Stats)
        player_.stop();
    jobs_.start(id, action, std::move(in));
    repaint();
}

void EditorWindow::collectJob()
{
    ModuleId id;
    int action = 0;
    bool ok = false;
    ModuleOutput out;
    std::string error;
    if (!jobs_.take(id, action, ok, out, error))
        return;
    if (!ok) {
        setStatus(error.empty() ? std::string("That didn't work.") : error, true);
        return;
    }
    ModuleState& st = modules_[static_cast<int>(id)];
    for (const auto& sv : out.setValues)
        if (sv.first < static_cast<int>(st.values.size()))
            st.values[static_cast<size_t>(sv.first)] = sv.second;
    if (!out.info.empty())
        st.info = out.info;
    if (!out.spectrumDb.empty()) {
        st.spectrumDb = std::move(out.spectrumDb);
        st.spectrumBinHz = out.spectrumBinHz;
    }
    if (!out.eqReference.empty()) {
        eqReference_ = std::move(out.eqReference);
        eqReferenceInfo_ = "Reference: " + fmt("%.2f s", (selEnd_ - selStart_) / doc_.audio().sampleRate) + " learned";
    }
    if (out.kind == ModuleOutput::Kind::Region && !out.region.empty()) {
        player_.stop();
        spec_.quiesce();
        doc_.replaceRegion(out.start, std::move(out.region), out.label);
        afterAudioChange(0.0);
    } else if (out.kind == ModuleOutput::Kind::Whole) {
        player_.stop();
        spec_.quiesce();
        const double rate = doc_.audio().sampleRate;
        doc_.replaceAll(std::move(out.whole), out.label);
        afterAudioChange(rate);
    }
    std::string msg = out.message;
    if (msg.empty() && !out.label.empty())
        msg = out.label + ".";
    if (out.kind != ModuleOutput::Kind::None)
        msg += "  Ctrl+Z to undo.";
    setStatus(msg);
}

// After undo/redo or a module, the file may have changed length or rate.
void EditorWindow::afterAudioChange(double oldRate)
{
    const double rate = doc_.audio().sampleRate;
    if (oldRate > 0 && std::abs(oldRate - rate) > 0.5) { // keep positions at the same times
        const double k = rate / oldRate;
        selStart_ = static_cast<size_t>(selStart_ * k);
        selEnd_ = static_cast<size_t>(selEnd_ * k);
        cursor_ = static_cast<size_t>(cursor_ * k);
        viewStart_ *= k;
        framesPerPx_ *= k;
    }
    const size_t n = numFrames();
    selStart_ = std::min(selStart_, n);
    selEnd_ = std::min(selEnd_, n);
    cursor_ = std::min(cursor_, n);
    if (!hasSelection())
        boxActive_ = false;
    if (doc_.changedLength() > n) { // whole-file change: lengths may differ
        peaksVersion_ = 0;
        overviewVersion_ = ~uint64_t(0);
        waveKey_ = {};
        clampView();
    }
}

int EditorWindow::laneAt(float y, float& pos) const
{
    if (!doc_.isOpen())
        return -1;
    const int nch = doc_.audio().numChannels();
    int wx, wy, ww, laneH, gap;
    laneGeometry(nch, wx, wy, ww, laneH, gap);
    const double sc = scale();
    for (int c = 0; c < nch; ++c) {
        const float ly = static_cast<float>((wy + c * (laneH + gap)) / sc), lh = static_cast<float>(laneH / sc);
        if (y >= ly && y < ly + lh) {
            pos = (y - ly) / lh;
            return c;
        }
    }
    return -1;
}

void EditorWindow::idleCallback()
{
    try {
        idleImpl();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Mend: idle update failed: %s\n", e.what());
    }
}

void EditorWindow::idleImpl()
{
    if (pendingInitialSize_ > 0 && --pendingInitialSize_ == 0) {
        const double sf = scale();
        uint w = static_cast<uint>(1320 * sf), h = static_cast<uint>(820 * sf);
        if (maxInitialW_ > 0)
            w = std::max(static_cast<uint>(kMinW * sf), std::min(w, maxInitialW_));
        if (maxInitialH_ > 0)
            h = std::max(static_cast<uint>(kMinH * sf), std::min(h, maxInitialH_));
        setSize(w, h);
        if (doc_.isOpen())
            fitViewAfterLayout_ = true;
    }
    if (fitViewAfterLayout_ && pendingInitialSize_ == 0) {
        layout();
        fitView();
        fitViewAfterLayout_ = false;
        repaint();
    }
    bool redraw = repaintAfterResize_ || (doc_.isOpen() && showSpectrogram() && (spec_.busy() || specWaiting()));
    repaintAfterResize_ = false;
    if (busy()) {
        redraw = true;
    } else {
        const uint64_t v = doc_.version();
        collectRender();
        collectJob();
        redraw |= v != doc_.version();
    }
    if (fileJob_ != nullptr) {
        finishFileJob();
        redraw = true;
    }
    if (updateLive())
        redraw = true;
    if (player_.takeLost()) { // e.g. the output was unplugged mid-play
        player_.reloadSettings();
        setStatus("Playback stopped: the audio output went away.", true);
        audioPanel_.refresh(true);
        redraw = true;
    }
    if (audioPanel_.idle())
        redraw = true;
    if (player_.playing()) {
        float l, r;
        player_.takePeaks(l, r);
        meterL_ = std::max(meterL_ * 0.85f, l);
        meterR_ = std::max(meterR_ * 0.85f, r);
        // keep the playhead in view
        const double pos = static_cast<double>(std::min(player_.position(), numFrames()));
        const double visible = wave_.w * framesPerPx_;
        if (pos > viewStart_ + visible * 0.97 || pos < viewStart_) {
            viewStart_ = pos - visible * 0.03;
            clampView();
        }
        redraw = true;
    } else if (meterL_ > 1e-4f || meterR_ > 1e-4f) {
        meterL_ *= 0.85f;
        meterR_ *= 0.85f;
        redraw = true;
    }
    if (redraw)
        repaint();
}


void EditorWindow::setViewBlend(float mx)
{
    float t = std::clamp((mx - blendTrack_.x) / std::max(1.0f, blendTrack_.w), 0.0f, 1.0f);
    for (float d : { 0.0f, 0.5f, 1.0f })
        if (std::abs(t - d) < 0.035f)
            t = d;
    if (t != viewBlend_) {
        viewBlend_ = t;
        repaint();
    }
}

// ---- live readings ------------------------------------------------------------

const ToolProcessor& EditorWindow::meterLayout(ToolId id)
{
    auto& p = meterLayout_[static_cast<size_t>(id)];
    if (!p)
        p = makeProcessor(id); // only its meter descriptions are used
    return *p;
}

float EditorWindow::metersHeight(ToolId id)
{
    const int n = meterLayout(id).meterCount();
    return n == 0 ? 0.0f : 10.0f + n * 18.0f;
}

float EditorWindow::drawMeters(ToolId id, float x, float y, float w)
{
    const ToolProcessor& layout = meterLayout(id);
    const int n = layout.meterCount();
    if (n == 0)
        return y;
    const size_t t = static_cast<size_t>(id);
    y += 6;
    const float labelW = 96, valueW = 62;
    for (int i = 0; i < n; ++i) {
        const auto& m = layout.meterInfo(i);
        const float v = meterShown_[t][static_cast<size_t>(i)];
        const float cy = y + i * 18.0f + 9.0f;
        text(x, cy, 11.0f, theme::textDim(), ALIGN_LEFT | ALIGN_MIDDLE, m.label);
        const Rect bar { x + labelW, cy - 3.5f, w - labelW - valueW, 7 };
        beginPath();
        roundedRect(bar.x, bar.y, bar.w, bar.h, 3.5f);
        fillColor(theme::well());
        fill();
        float a = bar.x, b = bar.x;
        if (m.bipolar) {
            const float mid = bar.x + bar.w * 0.5f, e = mid + std::clamp(v / m.max, -1.0f, 1.0f) * bar.w * 0.5f;
            a = std::min(mid, e);
            b = std::max(mid, e);
        } else {
            b = bar.x + std::clamp(v / m.max, 0.0f, 1.0f) * bar.w;
        }
        if (b - a > 0.5f) {
            beginPath();
            roundedRect(a, bar.y, b - a, bar.h, 3.5f);
            fillColor(m.cut ? theme::warm() : theme::accent());
            fill();
        }
        std::string label = "0.0 dB";
        if (std::abs(v) >= 0.05f)
            label = fmt(m.cut ? "-%.1f dB" : m.bipolar ? "%+.1f dB" : "+%.1f dB", v);
        text(x + w, cy, 11.0f, theme::text(), ALIGN_RIGHT | ALIGN_MIDDLE, label);
    }
    return y + n * 18.0f + 4.0f;
}

bool EditorWindow::updateLive()
{
    bool moved = false;
    for (size_t t = 0; t < liveTarget_.size(); ++t) {
        const ToolProcessor* p = player_.processor(static_cast<ToolId>(t));
        const int meters = chain_[t].enabled ? meterLayout(static_cast<ToolId>(t)).meterCount() : 0;
        for (int i = 0; i < kLive; ++i) {
            const size_t k = static_cast<size_t>(i);
            if (p != nullptr) {
                const float lv = p->liveValue(i);
                if (std::isfinite(lv))
                    liveTarget_[t][k] = lv;
                meterTarget_[t][k] = i < meters ? p->meterValue(i) : 0.0f;
            } else {
                meterTarget_[t][k] = 0.0f; // meters settle when not previewing; knobs keep the last reading
            }
            // glide the shown values towards their targets
            float& ls = liveShown_[t][k];
            const float lt = liveTarget_[t][k];
            if (std::isfinite(lt)) {
                const float next = std::isfinite(ls) ? ls + 0.3f * (lt - ls) : lt;
                if (!std::isfinite(ls) || std::abs(next - ls) > 1e-3f)
                    moved = true;
                ls = std::abs(next - lt) < 1e-3f ? lt : next;
            }
            float& ms = meterShown_[t][k];
            const float mt = meterTarget_[t][k];
            const float next = ms + 0.35f * (mt - ms);
            if (std::abs(next - ms) > 1e-3f)
                moved = true;
            ms = std::abs(next - mt) < 1e-3f ? mt : next;
        }
    }
    return moved;
}

void EditorWindow::drawChoiceList()
{
    const ToolParam& p = *choiceList_.param;
    const float W = static_cast<float>(Window::getWidth() / scale()), H = static_cast<float>(Window::getHeight() / scale());
    beginPath();
    rect(0, 0, W, H);
    fillColor(Color(0, 0, 0, 0.55f));
    fill();
    const int n = static_cast<int>(p.max - p.min) + 1;
    const bool voiceMics = choiceList_.chainTool == static_cast<int>(ToolId::Voice) && choiceList_.paramIndex == 0;
    const float colW = 210, rowH = 24;
    const int rowsPerCol = std::max(1, static_cast<int>((H - 160) / rowH));
    const int cols = (n + (voiceMics ? 6 : 0) + rowsPerCol - 1) / rowsPerCol;
    const float boxW = cols * colW + 32, boxH = std::min(H - 80, (std::min(n + (voiceMics ? 6 : 0), rowsPerCol)) * rowH + 60);
    const Rect box { (W - boxW) * 0.5f, (H - boxH) * 0.5f, boxW, boxH };
    mend::ui::drawPanel(*this, box);
    text(box.x + 18, box.y + 22, 12.0f, theme::textDim(), ALIGN_LEFT | ALIGN_MIDDLE, p.label);
    choiceRects_.clear();
    const int current = static_cast<int>(std::lround(*choiceList_.value));
    int row = 0, col = 0;
    std::string group;
    auto place = [&](float& x, float& y) {
        if (row >= rowsPerCol) {
            row = 0;
            ++col;
        }
        x = box.x + 16 + col * colW;
        y = box.y + 42 + row * rowH;
        ++row;
    };
    for (int i = 0; i < n; ++i) {
        float x, y;
        if (voiceMics && group != mend::VoiceClean::mic(i).group) { // headings
            group = mend::VoiceClean::mic(i).group;
            if (row >= rowsPerCol - 1) // keep a heading with its first entry
                row = rowsPerCol;
            place(x, y);
            text(x + 8, y + rowH * 0.5f + 2, 10.0f, theme::textFaint(), ALIGN_LEFT | ALIGN_MIDDLE, group);
        }
        place(x, y);
        const Rect r { x, y, colW - 10, rowH };
        if (i == current) {
            beginPath();
            roundedRect(r.x, r.y + 1, r.w, r.h - 2, 5);
            fillColor(theme::accentSoft());
            fill();
        }
        text(r.x + 8, r.cy(), 12.5f, i == current ? theme::accent() : theme::text(), ALIGN_LEFT | ALIGN_MIDDLE,
             p.choices[i]);
        choiceRects_.push_back({ r, i });
    }
}

END_NAMESPACE_DGL
