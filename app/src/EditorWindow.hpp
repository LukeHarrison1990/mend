// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#pragma once

#include "Document.hpp"
#include "Modules.hpp"
#include "Player.hpp"
#include "Renderer.hpp"
#include "Spectrogram.hpp"
#include "Tools.hpp"

#include "NanoVG.hpp"
#include "mend/ui/Widgets.hpp"

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

START_NAMESPACE_DGL

class EditorWindow : public NanoStandaloneWindow, public IdleCallback {
public:
    explicit EditorWindow(Application& app);
    ~EditorWindow() override;

    void showAtDefaultSize();
    // Largest physical size to open at (e.g. the screen's work area).
    void setMaxInitialSize(uint w, uint h) { maxInitialW_ = w; maxInitialH_ = h; }

private:
    int pendingInitialSize_ = 0;
    bool fitViewAfterLayout_ = false;
    float lastWaveW_ = 0;
    uint maxInitialW_ = 0, maxInitialH_ = 0;
    bool confirmDiscard_ = false;
    uint lastPhysW_ = 0, lastPhysH_ = 0;
    bool repaintAfterResize_ = false;

public:

    // Open a file directly (command line, drag and drop in future).
    void openPath(const std::string& path);

protected:
    void onNanoDisplay() override;
    bool onMouse(const MouseEvent& ev) override;
    bool onMotion(const MotionEvent& ev) override;
    bool onScroll(const ScrollEvent& ev) override;
    bool onKeyboard(const KeyboardEvent& ev) override;
    void onFileSelected(const char* filename) override;
    void idleCallback() override;
    void idleImpl();

private:
    using Rect = mend::ui::Rect;

    // ---- layout (logical units) ----
    double scale() const;
    float logicalW() const;
    float logicalH() const;
    void layout();
    Rect header_, main_, toolbar_, axis_, ruler_, wave_, overview_, panel_, panelList_, panelFooter_, transport_;

    // ---- drawing ----
    void drawFrame();
    void drawHeader();
    void drawEmptyState();
    void drawRuler();
    void drawWaveform();
    void drawToolbar();
    void drawAxis(int nch, int wy, int laneH, int gap);
    void laneGeometry(int nch, int& wx, int& wy, int& ww, int& laneH, int& gap) const;
    void updatePeaks();
    void uploadImage(std::unique_ptr<NanoImage>& img, int w, int h, const std::vector<uint8_t>& rgba, int flags);
    bool specWaiting() const { return specRequested_ != specShown_; }
    void drawOverview();
    void drawTransport();
    void drawPanel();
    float drawToolCard(mend::editor::ToolId id, float y);
    float drawModuleCard(mend::editor::ModuleId id, float y);
    // Toggles and choices as rows, knobs three to a row. chainTool >= 0
    // marks parameters of a chain tool (changes go to the preview).
    float drawParams(const std::vector<mend::editor::ToolParam>& params, std::vector<float>& values, float x, float y,
                     float w, int chainTool);
    static float paramsHeight(const std::vector<mend::editor::ToolParam>& params);
    void drawBusyOverlay();
    bool button(const Rect& r, const char* label, bool enabled, bool primary, std::function<void()> action);
    void iconButton(const Rect& r, int icon, bool enabled, bool active, std::function<void()> action);
    void text(float x, float y, float size, const mend::ui::Color& c, int align, const std::string& s);

    // ---- immediate-mode hit regions, rebuilt each frame ----
    struct Hit {
        Rect r;
        std::function<void()> click;
    };
    std::vector<Hit> hits_;
    struct KnobHit {
        Rect r;
        float* value;
        const mend::editor::ToolParam* param;
        int chainTool; // -1 for module parameters
        int index;     // parameter index within its tool
    };
    std::vector<KnobHit> knobHits_;

    // ---- actions ----
    void chooseOpen();
    void chooseExport();
    void doExport(const std::string& path);
    void togglePlay();
    void stopPlayback();
    void startRender();
    void collectRender();
    void undo();
    void redo();
    void learnNoise();
    void runModuleAction(mend::editor::ModuleId id, int action);
    void collectJob();
    void afterAudioChange(double oldRate);
    void paramChanged(int chainTool);
    bool busy() const { return renderer_.busy() || jobs_.busy(); }
    int laneAt(float y, float& pos) const; // lane index under y (logical), pos 0 top..1 bottom
    bool spectrumShown() const { return viewMode_ != ViewMode::Waveform; }
    void settingsChanged(bool enabledSetChanged);
    void setStatus(const std::string& s, bool error = false);

    // ---- view helpers ----
    double frameAtX(float x) const;
    float xAtFrame(double f) const;
    void zoomAround(double factor, float x);
    void fitView();
    void clampView();
    bool hasSelection() const { return selEnd_ > selStart_; }
    size_t numFrames() const { return doc_.audio().numFrames(); }
    std::string timeString(double frames, bool withMs = true) const;

    // ---- state ----
    mend::editor::Document doc_;
    mend::editor::PeakCache peaks_;
    uint64_t peaksVersion_ = 0;
    mend::editor::Player player_;
    mend::editor::Renderer renderer_;
    mend::editor::ChainSettings chain_;
    std::vector<bool> expanded_;
    mend::editor::JobRunner jobs_;
    struct ModuleState {
        std::vector<float> values;
        bool expanded = false;
        std::vector<std::string> info;
        std::vector<double> spectrumDb;
        double spectrumBinHz = 0.0;
    };
    std::vector<ModuleState> modules_;
    std::vector<double> eqReference_;
    std::string eqReferenceInfo_;
    int panelTab_ = 0; // 0 repair chain, 1 utility modules
    float tabScroll_[2] = { 0.0f, 0.0f };

    // spectral (box) selection: the time selection plus a frequency band
    enum class SelectTool { Time, Box };
    SelectTool selectTool_ = SelectTool::Time;
    bool boxActive_ = false;
    double boxLoHz_ = 0.0, boxHiHz_ = 0.0, boxAnchorHz_ = 0.0;

    // view: waveform and/or spectrogram, drawn from cached images
    enum class ViewMode { Waveform, Spectrogram, Both };
    ViewMode viewMode_ = ViewMode::Both;
    mend::editor::FreqScale freqScale_ = mend::editor::FreqScale::Mel;
    float specFloorDb_ = -110.0f;
    mend::editor::SpectrogramWorker spec_;
    mend::editor::SpecView specRequested_, specShown_;
    std::vector<std::unique_ptr<NanoImage>> specImg_;
    struct WaveKey {
        double viewStart = -1, fpc = 0;
        int w = 0, h = 0;
        uint64_t version = 0;
        ViewMode mode = ViewMode::Waveform;
        bool operator==(const WaveKey& o) const
        {
            return viewStart == o.viewStart && fpc == o.fpc && w == o.w && h == o.h && version == o.version && mode == o.mode;
        }
    };
    WaveKey waveKey_;
    std::vector<std::unique_ptr<NanoImage>> waveImg_;
    std::vector<uint8_t> waveRgba_;
    NanoImage logo_;
    std::vector<float> overviewLo_, overviewHi_;
    uint64_t overviewVersion_ = ~uint64_t(0);

    double viewStart_ = 0.0;   // frames
    double framesPerPx_ = 1.0; // frames per logical unit
    size_t cursor_ = 0;
    size_t selStart_ = 0, selEnd_ = 0;
    bool previewOn_ = true;
    float panelScroll_ = 0.0f, panelContentH_ = 0.0f;
    float meterL_ = 0.0f, meterR_ = 0.0f;

    enum class Drag { None, Select, Box, SelStartEdge, SelEndEdge, Overview, Knob };
    Drag drag_ = Drag::None;
    double dragAnchor_ = 0.0;
    KnobHit dragKnob_ {};
    double dragStartY_ = 0.0;
    float dragStartNorm_ = 0.0f;
    std::chrono::steady_clock::time_point lastKnobClick_ {};
    const float* lastKnobValue_ = nullptr;

    enum class Browse { None, Open, Export };
    Browse browsing_ = Browse::None;

    std::string status_;
    bool statusError_ = false;
    std::string lastDir_;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EditorWindow)
};

END_NAMESPACE_DGL
