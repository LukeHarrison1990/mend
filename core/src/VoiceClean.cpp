// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#include "mend/VoiceClean.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>

namespace mend {

namespace {
using B = VoiceClean::Band;
constexpr B kNone {};
// constexpr, so the tables below are built at compile time (other files
// read them during start-up).
constexpr B ls(double hz, double db) { return { B::LowShelf, hz, db, 0.7071 }; }
constexpr B pk(double hz, double db, double q) { return { B::Peak, hz, db, q }; }
constexpr B hs(double hz, double db) { return { B::HighShelf, hz, db, 0.7071 }; }

double coeffFor(double seconds, double rate) { return 1.0 - std::exp(-1.0 / (seconds * rate)); }

// Each profile undoes the main deviations from flat of one microphone
// (or kind of microphone), within +-5 dB, towards a neutral voice.
//
// [chart]    built from the manufacturer's published on-axis frequency
//            response (cardioid where there is a choice); README.md lists
//            the source of each.
// [estimate] no published chart could be found: a deliberately gentle
//            correction (at most 2 dB) from the model's general character.
//
// Names identify which microphone a profile is meant for; they are
// trademarks of their owners and imply no endorsement. Append new entries
// at the end of a group only before a release: hosts and projects store
// the index.
constexpr VoiceClean::MicProfile kMics[] = {
    // name                         group        HP     corrections                                                    clarity smooth demud
    { "Generic / unknown",          "General",   80,  { kNone, kNone, kNone, kNone },                                      50, 50, 50 },
    // [estimate] small laptop capsules: thin, a little honky and edgy
    { "Laptop built-in",            "General",  140,  { ls(220, 2.0), pk(1000, -1.5, 1.0), pk(4500, -1.0, 1.4), kNone },   45, 55, 35 },
    // [estimate]
    { "Headset",                    "General",  110,  { ls(200, 1.5), pk(1200, -1.0, 1.2), hs(6000, 1.0), kNone },        50, 50, 35 },
    // [estimate] chest resonance around 700 Hz, shadowed highs
    { "Lavalier (lapel)",           "General",   90,  { pk(750, -2.0, 1.2), pk(3500, 1.0, 0.9), hs(6000, 1.5), kNone },    55, 45, 55 },
    // [estimate]
    { "Shotgun (camera mount)",     "General",   85,  { ls(220, 1.0), pk(6000, -1.0, 1.4), kNone, kNone },                45, 45, 40 },
    // [chart] +1.5 dB at 50-300 Hz, sloping to -3.5 dB from 5 kHz up
    { "AEA R84",                    "Studio",    80,  { ls(250, -1.5), hs(4000, 3.5), kNone, kNone },                      55, 45, 50 },
    // [chart] cardioid: flat to 10 kHz, about +1.5 dB around 12-15 kHz
    { "AKG C414 XLS",               "Studio",    75,  { pk(13000, -1.5, 1.5), kNone, kNone, kNone },                       45, 45, 40 },
    // [chart] within +-1 dB 80 Hz-6 kHz, +2 dB around 8-10 kHz
    { "Electro-Voice RE20",         "Studio",    70,  { pk(9000, -1.5, 1.2), kNone, kNone, kNone },                        55, 50, 55 },
    // [chart] flat to 3 kHz, then a plateau about +4 dB from 6 to 16 kHz
    { "Neumann TLM 103",            "Studio",    75,  { hs(5000, -3.5), kNone, kNone, kNone },                             45, 45, 45 },
    // [estimate]
    { "RODE NT1",                   "Studio",    75,  { pk(10000, -1.0, 1.0), kNone, kNone, kNone },                       40, 45, 45 },
    // [estimate] dark end-address dynamic
    { "RODE PodMic",                "Studio",    80,  { pk(280, -1.0, 1.0), pk(4000, 1.5, 0.8), hs(9000, 1.5), kNone },    60, 50, 55 },
    // [chart] flat 100 Hz-1 kHz, presence peak about +8 dB near 4.5 kHz, +5 dB at 8-15 kHz
    { "Sennheiser MD 421",          "Studio",    90,  { pk(4500, -5.0, 0.9), pk(11000, -3.0, 1.0), kNone, kNone },         40, 50, 55 },
    // [chart] flat 100 Hz-1 kHz, +5 dB presence peak at 4.5-6 kHz, +4 dB at 10 kHz
    { "Shure SM58",                 "Studio",    90,  { pk(5000, -4.0, 0.9), pk(10000, -2.5, 1.4), kNone, kNone },         45, 55, 60 },
    // [chart] flat setting: level from 200 Hz to 8 kHz, falling above 12 kHz (-14 dB at 18 kHz)
    { "Shure SM7B",                 "Studio",    70,  { hs(12000, 3.0), kNone, kNone, kNone },                             55, 50, 50 },
    // [chart] (published for the AT2020USB; the "+" model keeps that design) flat, +2 dB near 10 kHz
    { "Audio-Technica AT2020USB+",  "USB",       80,  { pk(9500, -1.5, 2.0), kNone, kNone, kNone },                        45, 50, 55 },
    // [chart] (published for the Snowball's cardioid mode, which the iCE shares):
    // everything below about 800 Hz sits 4-5 dB under 1 kHz, a 2-3 dB dip around 5 kHz
    { "Blue Snowball iCE",          "USB",       90,  { ls(600, 4.0), pk(5000, 2.5, 1.0), kNone, kNone },                  50, 50, 40 },
    // [chart] cardioid: about -9 dB at 50 Hz, -2 dB dip at 2.5-3 kHz, -3 dB at 15 kHz
    { "Blue Yeti",                  "USB",       85,  { ls(120, 2.0), pk(2800, 2.0, 1.2), hs(12000, 2.5), kNone },         50, 50, 50 },
    // [estimate]
    { "Blue Yeti Nano",             "USB",       85,  { pk(3000, 1.0, 1.2), kNone, kNone, kNone },                         50, 50, 50 },
    // [estimate]
    { "Blue Yeti X",                "USB",       80,  { pk(6000, -1.0, 1.4), kNone, kNone, kNone },                        45, 50, 50 },
    // [estimate]
    { "RODE NT-USB",                "USB",       80,  { pk(300, -1.0, 1.0), pk(10000, -1.0, 1.0), kNone, kNone },          45, 50, 50 },
    // [estimate]
    { "Samson G-Track Pro",         "USB",       85,  { pk(400, -1.5, 1.0), pk(4000, 1.0, 0.9), kNone, kNone },            55, 50, 55 },
    // [estimate] handheld dynamic, close-worked
    { "Samson Q2U",                 "USB",       90,  { ls(180, -1.0), pk(3500, 1.0, 0.9), hs(9000, 1.5), kNone },         55, 55, 55 },
    // [estimate] typical inexpensive USB condenser: boxy, edgy
    { "Budget USB microphone",      "USB",      100,  { pk(450, -1.5, 0.9), pk(3000, 1.0, 0.9), pk(7000, -1.0, 1.5), kNone }, 55, 55, 55 },
    // [chart] rolls off below about 200 Hz, flat to 8 kHz, falling above 10 kHz
    { "Sennheiser MKE 400",         "Camera",    80,  { ls(150, 3.0), hs(12000, 3.0), kNone, kNone },                      45, 45, 40 },
    // [estimate] (no published charts for earbuds or phones) band-limited, processed
    { "Apple AirPods",              "Built-in", 120,  { ls(200, 2.0), pk(1000, -1.0, 1.0), pk(3000, 1.0, 1.0), kNone },    60, 35, 30 },
    // [estimate]
    { "Apple EarPods (wired)",      "Built-in", 110,  { ls(220, 1.0), pk(1200, -1.5, 1.2), hs(5000, 1.0), kNone },         55, 45, 35 },
    // [estimate]
    { "iPhone built-in",            "Built-in", 100,  { ls(180, 2.0), pk(1500, -1.0, 1.0), pk(5000, -1.0, 1.5), kNone },   50, 40, 30 },
    // [estimate]
    { "Android phone built-in",     "Built-in", 110,  { ls(200, 2.0), pk(1500, -1.0, 1.0), pk(5000, -1.0, 1.5), kNone },   50, 40, 30 },
};

static_assert(sizeof(kMics) / sizeof(kMics[0]) == VoiceClean::kNumMics, "update kNumMics");

constexpr VoiceClean::Character kCharacters[] = {
    { "Flat", { kNone, kNone, kNone, kNone }, 0.0, true },
    { "Body and clarity", { ls(180, 2.5), pk(3500, 2.0, 0.8), kNone, kNone }, 0.0, true },
    { "De-harsh", { pk(3200, -3.0, 1.2), hs(8000, -2.0), kNone, kNone }, 3.0, true },
    { "Hi-fi", { ls(110, 1.5), pk(450, -1.5, 0.9), hs(10000, 3.0), kNone }, 0.0, true },
    // Video-call audio arrives band-limited, nasal and already gated.
    { "Video call fix", { ls(160, 3.0), pk(1100, -2.0, 1.0), pk(4000, 2.5, 0.9), hs(8000, 2.0) }, 1.0, false },
};

static_assert(sizeof(kCharacters) / sizeof(kCharacters[0]) == VoiceClean::kNumCharacters, "update kNumCharacters");

BiquadCoeffs bandCoeffs(double fs, const B& b)
{
    const double hz = std::min(b.hz, fs * 0.45);
    switch (b.type) {
    case B::LowShelf:
        return BiquadCoeffs::lowShelf(fs, hz, dbToGain(b.db));
    case B::HighShelf:
        return BiquadCoeffs::highShelf(fs, hz, dbToGain(b.db));
    case B::Peak:
        return BiquadCoeffs::peaking(fs, hz, b.q, dbToGain(b.db));
    default:
        return {};
    }
}

double responseOf(const BiquadCoeffs& c, double hz, double fs)
{
    const double w = 2.0 * kPi * hz / fs;
    const std::complex<double> z1 = std::polar(1.0, -w), z2 = z1 * z1;
    const std::complex<double> h = (c.b0 + c.b1 * z1 + c.b2 * z2) / (1.0 + c.a1 * z1 + c.a2 * z2);
    return std::abs(h);
}

constexpr double kTargetDb = -22.0; // levelled speech RMS (dBFS)
} // namespace

int VoiceClean::numMics() { return static_cast<int>(sizeof(kMics) / sizeof(kMics[0])); }
const VoiceClean::MicProfile& VoiceClean::mic(int i) { return kMics[std::clamp(i, 0, numMics() - 1)]; }
int VoiceClean::numCharacters() { return static_cast<int>(sizeof(kCharacters) / sizeof(kCharacters[0])); }
const VoiceClean::Character& VoiceClean::character(int i)
{
    return kCharacters[std::clamp(i, 0, numCharacters() - 1)];
}

// ---------------------------------------------------------------------------
// Resonance smoothing. A long-term average spectrum of the voice is kept;
// where it stands above a smoothed copy of itself (a fixed peak: a room
// mode, a microphone or cheap-capsule resonance) that frequency is turned
// down. Voice harmonics move with pitch, so they blur out of the average
// and are left alone.

struct VoiceClean::ResonanceSmoother : Stft::FrameProcessor {
    int bins = 0;
    double fs = 48000.0, binHz = 46.875;
    double depthDb = 0.0;   // maximum cut
    double avgCoef = 0.0;   // long-term average update per frame
    std::vector<double> avg, logAvg, smoothed, cutDb, gain;
    std::vector<int> lo, hi; // smoothing window per bin
    std::atomic<float> deepest { 0.0f };
    // Low-mid balance of the long-term spectrum: 200-500 Hz against
    // 1-2.5 kHz (equal widths in octaves), in dB. NaN until known.
    std::atomic<float> mudShareDb { std::numeric_limits<float>::quiet_NaN() };
    bool learned = false;
    std::vector<float> cutShared; // copy for drawing
    std::atomic<int> cutSerial { 0 };

    void prepare(int nBins, double sampleRate, int fftSize, int hop)
    {
        bins = nBins;
        fs = sampleRate;
        binHz = fs / fftSize;
        avg.assign(bins, 0.0);
        logAvg.assign(bins, -120.0);
        smoothed.assign(bins, -120.0);
        cutDb.assign(bins, 0.0);
        gain.assign(bins, 1.0);
        cutShared.assign(bins, 0.0f);
        lo.assign(bins, 0);
        hi.assign(bins, 0);
        for (int k = 0; k < bins; ++k) { // +-1/3 octave
            lo[k] = std::max(0, static_cast<int>(std::floor(k / 1.26)));
            hi[k] = std::min(bins - 1, static_cast<int>(std::ceil(k * 1.26)));
        }
        avgCoef = coeffFor(4.0, sampleRate / hop);
    }

    void reset()
    {
        std::fill(avg.begin(), avg.end(), 0.0);
        std::fill(cutDb.begin(), cutDb.end(), 0.0);
        std::fill(gain.begin(), gain.end(), 1.0);
        deepest.store(0.0f);
        mudShareDb.store(std::numeric_limits<float>::quiet_NaN());
        learned = false;
    }

    void processFrames(std::complex<double>* const* frame, int numBins, int numChannels) override
    {
        // Linked across channels: one gain curve from the summed power.
        double total = 0.0;
        for (int k = 0; k < numBins; ++k) {
            double p = 0.0;
            for (int c = 0; c < numChannels; ++c)
                p += std::norm(frame[c][k]);
            smoothed[k] = p; // temporarily: this frame's power
            total += p;
        }
        // Learn only from frames with signal in them (not the gaps).
        const bool active = total > 1e-6 * numBins;
        if (active) {
            // Start from the first frame rather than fading in from silence.
            const double a = learned ? avgCoef : 1.0;
            for (int k = 0; k < numBins; ++k)
                avg[k] += a * (smoothed[k] - avg[k]);
            learned = true;
            double mud = 0.0, ref = 0.0;
            for (int k = 0; k < numBins; ++k) {
                const double hz = k * binHz;
                if (hz >= 200.0 && hz < 500.0)
                    mud += avg[k];
                else if (hz >= 1000.0 && hz < 2500.0)
                    ref += avg[k];
            }
            if (ref > 0.0 && mud > 0.0)
                mudShareDb.store(static_cast<float>(10.0 * std::log10(mud / ref)));
        }

        for (int k = 0; k < numBins; ++k)
            logAvg[k] = 10.0 * std::log10(avg[k] + 1e-20);
        // Smoothed copy (mean in dB over +-1/3 octave), via prefix sums.
        double run = 0.0;
        std::vector<double>& prefix = smoothed;
        for (int k = 0; k < numBins; ++k) {
            run += logAvg[k];
            prefix[k] = run;
        }
        float deep = 0.0f;
        for (int k = 0; k < numBins; ++k) {
            const double hz = k * binHz;
            double cut = 0.0;
            if (depthDb > 0.0 && hz >= 250.0 && hz <= 9000.0) {
                const int a = lo[k], b = hi[k];
                const double mean = (prefix[b] - (a > 0 ? prefix[a - 1] : 0.0)) / (b - a + 1);
                const double excess = logAvg[k] - mean - 2.0; // 2 dB grace
                cut = std::clamp(excess * 0.8, 0.0, depthDb);
            }
            cutDb[k] += 0.1 * (cut - cutDb[k]); // glide
            gain[k] = dbToGain(-cutDb[k]);
            deep = std::max(deep, static_cast<float>(cutDb[k]));
            cutShared[k] = static_cast<float>(-cutDb[k]);
        }
        deepest.store(deep);
        cutSerial.fetch_add(1);
        for (int c = 0; c < numChannels; ++c)
            for (int k = 0; k < numBins; ++k)
                frame[c][k] *= gain[k];
    }

    void processFrame(std::complex<double>*, int, int) override {}
};

// ---------------------------------------------------------------------------

VoiceClean::VoiceClean()
    : res_(std::make_unique<ResonanceSmoother>())
{
}

VoiceClean::~VoiceClean() = default;

void VoiceClean::prepare(double sampleRate, int numChannels)
{
    fs_ = sampleRate;
    nch_ = std::clamp(numChannels, 1, 8);

    int fft = 1024;
    while (fft < sampleRate * 0.02) // ~21 ms at any rate
        fft <<= 1;
    stft_.prepare(fft, 4, nch_);
    res_->prepare(stft_.numBins(), sampleRate, fft, stft_.hopSize());

    deEss_.prepare(sampleRate, nch_);

    limLook_ = std::max(16, static_cast<int>(sampleRate * 0.0015));
    limDelay_.prepare(nch_, limLook_);
    limPeaks_.assign(static_cast<size_t>(limLook_ + 1), 0.0f);
    limRel_ = coeffFor(0.060, sampleRate);
    bufA_.assign(static_cast<size_t>(nch_), std::vector<float>(kChunk));
    bufB_ = bufA_;

    levelCoef_ = coeffFor(0.4, sampleRate);
    fastAtt_ = coeffFor(0.0008, sampleRate);
    fastRel_ = coeffFor(0.025, sampleRate);
    slowAtt_ = coeffFor(0.020, sampleRate);
    slowRel_ = coeffFor(0.150, sampleRate);
    compAtt_ = coeffFor(0.006, sampleRate);
    compRel_ = coeffFor(0.120, sampleRate);
    compEnvCoef_ = coeffFor(0.010, sampleRate);

    mudFilter_.assign(nch_, Biquad {});
    clar_.assign(nch_, ClarityCh {});
    const double f1 = 700.0, f2 = std::min(3500.0, sampleRate * 0.4);
    const auto lp1 = BiquadCoeffs::lowpass(sampleRate, f1, 0.70710678), hp1 = BiquadCoeffs::highpass(sampleRate, f1, 0.70710678);
    const auto lp2 = BiquadCoeffs::lowpass(sampleRate, f2, 0.70710678), hp2 = BiquadCoeffs::highpass(sampleRate, f2, 0.70710678);
    for (auto& c : clar_) {
        c.lp1a.setCoeffs(lp1), c.lp1b.setCoeffs(lp1), c.hp1a.setCoeffs(hp1), c.hp1b.setCoeffs(hp1);
        c.lp2a.setCoeffs(lp2), c.lp2b.setCoeffs(lp2), c.hp2a.setCoeffs(hp2), c.hp2b.setCoeffs(hp2);
        c.apLa.setCoeffs(lp2), c.apLb.setCoeffs(lp2), c.apHa.setCoeffs(hp2), c.apHb.setCoeffs(hp2);
    }
    design();
    reset();
}

void VoiceClean::reset()
{
    for (auto& ch : eq_)
        for (auto& b : ch)
            b.reset();
    for (int c = 0; c < nch_; ++c) {
        mudFilter_[c].reset();
    }
    for (auto& c : clar_) {
        for (Biquad* b : { &c.lp1a, &c.lp1b, &c.hp1a, &c.hp1b, &c.lp2a, &c.lp2b, &c.hp2a, &c.hp2b, &c.apLa, &c.apLb,
                           &c.apHa, &c.apHb })
            b->reset();
    }
    levelEnv_ = 0.0;
    levelGainDb_ = 0.0;
    mudCutDb_ = 0.0;
    mudCounter_ = 0;
    for (int b = 0; b < 2; ++b) {
        fastEnv_[b] = slowEnv_[b] = 0.0;
        clarityGainSm_[b] = 1.0;
    }
    compEnv_ = 0.0;
    compGainDb_ = 0.0;
    stft_.reset();
    res_->reset();
    deEss_.reset();
    limDelay_.reset();
    std::fill(limPeaks_.begin(), limPeaks_.end(), 0.0f);
    limPos_ = 0;
    limGain_ = 1.0;
    peakHold_ = 0.0;
}

void VoiceClean::setSettings(const Settings& s)
{
    const bool redesign = s.mic != settings_.mic || s.character != settings_.character || s.deMud != settings_.deMud;
    settings_ = s;
    if (redesign)
        design();
    const auto& ch = character(settings_.character);
    res_->depthDb = 6.0 * settings_.smooth / 100.0 + ch.extraResonanceDb;

    DeEss::Settings d;
    d.frequency = 6000.0;
    d.thresholdDb = -27.0 - 7.0 * settings_.clarity / 100.0 - (settings_.character == 2 ? 3.0 : 0.0);
    // Part of both Clarity (brighter means more sibilance) and Smooth.
    d.maxReductionDb = 8.0 * std::max(settings_.clarity, settings_.smooth) / 100.0;
    deEss_.setSettings(d);
}

void VoiceClean::addBands(const Band* bands, int n)
{
    for (int i = 0; i < n; ++i)
        if (bands[i].type != Band::None && std::abs(bands[i].db) > 0.01)
            eqCoeffs_.push_back(bandCoeffs(fs_, bands[i]));
}

void VoiceClean::design()
{
    eqCoeffs_.clear();
    const auto& m = mic(settings_.mic);
    // 4th-order Butterworth high-pass.
    eqCoeffs_.push_back(BiquadCoeffs::highpass(fs_, m.highPassHz, 0.5411961));
    eqCoeffs_.push_back(BiquadCoeffs::highpass(fs_, m.highPassHz, 1.3065630));
    addBands(m.bands, 4);
    addBands(character(settings_.character).bands, 4);
    // De-mud's fixed part: a gentle, broad low-mid dip.
    const double fixedMud = 2.5 * settings_.deMud / 100.0;
    if (fixedMud > 0.01)
        eqCoeffs_.push_back(BiquadCoeffs::peaking(fs_, 300.0, 0.8, dbToGain(-fixedMud)));

    eq_.resize(static_cast<size_t>(nch_));
    for (auto& ch : eq_) {
        if (ch.size() != eqCoeffs_.size())
            ch.assign(eqCoeffs_.size(), Biquad {});
        for (size_t i = 0; i < ch.size(); ++i)
            ch[i].setCoeffs(eqCoeffs_[i]);
    }
}

int VoiceClean::latency() const noexcept { return stft_.latency() + deEss_.latency() + limLook_; }

double VoiceClean::staticResponseDb(double hz) const
{
    double g = 1.0;
    for (const auto& c : eqCoeffs_)
        g *= responseOf(c, hz, fs_);
    return gainToDb(g);
}

double VoiceClean::resonanceCutDb(double hz) const
{
    const auto& v = res_->cutShared;
    if (v.empty())
        return 0.0;
    const double k = hz / res_->binHz;
    const size_t i = std::min(v.size() - 1, static_cast<size_t>(std::max(0.0, k)));
    return v[i];
}

void VoiceClean::process(const float* const* in, float* const* out, int numChannels, int numSamples)
{
    // Work in chunks that fit the buffers allocated in prepare().
    const float* ip[8];
    float* op[8];
    const int nc = std::min(numChannels, 8);
    for (int done = 0; done < numSamples; done += kChunk) {
        const int n = std::min(kChunk, numSamples - done);
        for (int c = 0; c < nc; ++c) {
            ip[c] = in[c] + done;
            op[c] = out[c] + done;
        }
        processChunk(ip, op, nc, n);
    }
    for (int c = nc; c < numChannels; ++c)
        std::copy(in[c], in[c] + numSamples, out[c]);
}

void VoiceClean::processChunk(const float* const* in, float* const* out, int numChannels, int numSamples)
{
    const int nch = std::min(numChannels, nch_);

    const double clarity = settings_.clarity / 100.0, smooth = settings_.smooth / 100.0, deMud = settings_.deMud / 100.0;
    const bool expander = character(settings_.character).expander;
    const double levelAmount = std::min(1.0, smooth * 2.0);         // levelling reaches full at Smooth 50
    const double maxLevel = 10.0 * levelAmount;
    const double ratio = 1.0 + 3.0 * smooth;                        // 1:1 .. 4:1
    const double thr = kTargetDb - 4.0;                             // compression threshold (RMS dBFS)
    // Make-up: undo the typical compression at the levelled speech level,
    // and lift levelled speech to about -18 dBFS RMS (finished-podcast level).
    const double makeup = (kTargetDb - thr) * (1.0 - 1.0 / ratio) + 4.0 * levelAmount;
    const double mudMax = 9.0 * deMud;
    const double fastPow = 0.35 * clarity, maxBoost = dbToGain(6.0 * clarity);
    const double presence[2] = { dbToGain(1.5 * clarity), dbToGain(3.0 * clarity) };

    // ---- stage 1: EQ, levelling, de-mud, clarity (per sample) --------------------
    for (int i = 0; i < numSamples; ++i) {
        // high-pass + fixed EQ
        double x[8];
        double link = 0.0;
        for (int c = 0; c < nch; ++c) {
            double v = in[c][i];
            for (auto& b : eq_[c])
                v = b.process(v);
            x[c] = v;
            link += v * v;
        }
        link /= nch;

        // levelling: slow RMS, only while there is speech
        if (levelAmount > 0.0) {
            if (link > 1e-6) // above about -60 dBFS
                levelEnv_ += levelCoef_ * (link - levelEnv_);
            if (levelEnv_ > 1e-7) {
                const double want = std::clamp(kTargetDb - 10.0 * std::log10(levelEnv_), -maxLevel, maxLevel);
                levelGainDb_ += 2e-5 * 48000.0 / fs_ * (want - levelGainDb_); // ~1 s glide
            }
        } else {
            levelGainDb_ *= 0.999;
        }
        const double lg = dbToGain(levelGainDb_);

        for (int c = 0; c < nch; ++c)
            x[c] *= lg;

        // de-mud: steer a low-mid cut until the voice's long-term balance
        // (measured by the resonance stage, downstream of the cut) is
        // healthy. Natural speech keeps about 8 dB more level in 200-500 Hz
        // than in 1-2.5 kHz; more than that is build-up.
        if (++mudCounter_ >= 32) {
            mudCounter_ = 0;
            const float share = res_->mudShareDb.load();
            if (deMud > 0.0 && std::isfinite(share)) {
                const double target = 12.0 - 3.0 * deMud;                // 9 dB at full De-mud
                const double err = share + 1.5 * clarity - target;       // undo Clarity's presence lift
                mudCutDb_ = std::clamp(mudCutDb_ + 0.003 * 48000.0 / fs_ * err, 0.0, mudMax);
            } else {
                mudCutDb_ = std::max(0.0, mudCutDb_ - 0.01);
            }
            const auto mc = BiquadCoeffs::peaking(fs_, 330.0, 0.9, dbToGain(-mudCutDb_));
            for (int c = 0; c < nch; ++c)
                mudFilter_[c].setCoeffs(mc);
        }

        // clarity: split into three bands, emphasise attacks in the upper two
        double bands[8][3];
        double bandPow[2] = { 0.0, 0.0 };
        for (int c = 0; c < nch; ++c) {
            const double v = mudFilter_[c].process(x[c]);
            ClarityCh& s = clar_[c];
            double low = s.lp1b.process(s.lp1a.process(v));
            const double up = s.hp1b.process(s.hp1a.process(v));
            const double mid = s.lp2b.process(s.lp2a.process(up));
            const double high = s.hp2b.process(s.hp2a.process(up));
            low = s.apLb.process(s.apLa.process(low)) + s.apHb.process(s.apHa.process(low));
            // Linkwitz-Riley bands: low (all-passed at f2) + mid + high
            // sums to a flat all-pass response.
            bands[c][0] = low;
            bands[c][1] = mid;
            bands[c][2] = high;
            bandPow[0] += mid * mid;
            bandPow[1] += high * high;
        }
        double gains[2];
        for (int b = 0; b < 2; ++b) {
            const double p = std::sqrt(bandPow[b] / nch);
            fastEnv_[b] += (p > fastEnv_[b] ? fastAtt_ : fastRel_) * (p - fastEnv_[b]);
            slowEnv_[b] += (p > slowEnv_[b] ? slowAtt_ : slowRel_) * (p - slowEnv_[b]);
            double g = 1.0;
            if (fastPow > 0.0 && slowEnv_[b] > 1e-5)
                g = std::clamp(std::pow(fastEnv_[b] / slowEnv_[b], fastPow), 1.0, maxBoost);
            clarityGainSm_[b] += 0.02 * (g - clarityGainSm_[b]);
            gains[b] = clarityGainSm_[b] * presence[b];
        }
        for (int c = 0; c < nch; ++c)
            bufA_[c][i] = static_cast<float>(bands[c][0] + bands[c][1] * gains[0] + bands[c][2] * gains[1]);

        if (i == numSamples - 1) {
            readout_.levelDb.store(static_cast<float>(levelGainDb_));
            readout_.deMudDb.store(static_cast<float>(mudCutDb_ + 2.5 * deMud));
            readout_.clarityDb.store(static_cast<float>(gainToDb(std::max(clarityGainSm_[0], clarityGainSm_[1]))));
        }
    }

    // ---- stage 2: resonance smoothing (STFT) -------------------------------------
    {
        const float* ip[8];
        float* op[8];
        for (int c = 0; c < nch; ++c) {
            ip[c] = bufA_[c].data();
            op[c] = bufB_[c].data();
        }
        stft_.process(ip, op, nch, numSamples, *res_);
        readout_.resonanceDb.store(res_->deepest.load());
    }

    // ---- stage 3: compression and expansion (linked) -------------------------------
    for (int i = 0; i < numSamples; ++i) {
        double p = 0.0;
        for (int c = 0; c < nch; ++c)
            p += static_cast<double>(bufB_[c][i]) * bufB_[c][i];
        compEnv_ += compEnvCoef_ * (p / nch - compEnv_);
        const double lv = 10.0 * std::log10(compEnv_ + 1e-20);
        double gr = 0.0;
        if (smooth > 0.0) {
            const double knee = 8.0, over = lv - thr;
            if (over > knee * 0.5)
                gr = over * (1.0 - 1.0 / ratio);
            else if (over > -knee * 0.5)
                gr = (1.0 - 1.0 / ratio) * (over + knee * 0.5) * (over + knee * 0.5) / (2.0 * knee);
            // Expansion: ease down the gaps between phrases (breaths, room).
            const double floor = -52.0;
            if (expander && lv < floor && lv > -100.0) // not for digital silence
                gr += std::min(8.0 * smooth, (floor - lv) * 0.5 * smooth);
        }
        compGainDb_ += (gr > compGainDb_ ? compAtt_ : compRel_) * (gr - compGainDb_);
        const double g = dbToGain(makeup * (smooth > 0.0 ? 1.0 : 0.0) - compGainDb_);
        for (int c = 0; c < nch; ++c)
            bufB_[c][i] = static_cast<float>(bufB_[c][i] * g);
    }
    readout_.compressDb.store(static_cast<float>(compGainDb_));

    // ---- stage 4: de-ess -------------------------------------------------------------
    {
        const float* ip[8];
        float* op[8];
        for (int c = 0; c < nch; ++c) {
            ip[c] = bufB_[c].data();
            op[c] = bufA_[c].data();
        }
        deEss_.process(ip, op, nch, numSamples);
        readout_.deEssDb.store(static_cast<float>(deEss_.currentReductionDb()));
    }

    // ---- stage 5: output gain and peak limiter ----------------------------------------
    const double outGain = dbToGain(settings_.outputDb);
    const float ceiling = static_cast<float>(dbToGain(-1.0));
    const int W = static_cast<int>(limPeaks_.size());
    double peak = 0.0;
    double deepestLimit = 1.0;
    for (int i = 0; i < numSamples; ++i) {
        float pk = 0.0f;
        for (int c = 0; c < nch; ++c) {
            bufA_[c][i] = static_cast<float>(bufA_[c][i] * outGain);
            pk = std::max(pk, std::abs(bufA_[c][i]));
        }
        limPeaks_[static_cast<size_t>(limPos_)] = pk;
        limPos_ = (limPos_ + 1) % W;
        float windowPeak = 0.0f;
        for (float v : limPeaks_)
            windowPeak = std::max(windowPeak, v);
        const double want = windowPeak > ceiling ? ceiling / windowPeak : 1.0;
        limGain_ = want < limGain_ ? want : limGain_ + limRel_ * (want - limGain_);
        deepestLimit = std::min(deepestLimit, limGain_);
        for (int c = 0; c < nch; ++c) {
            const float y = limDelay_.push(c, bufA_[c][i]) * static_cast<float>(limGain_);
            out[c][i] = std::clamp(y, -ceiling, ceiling);
            peak = std::max(peak, static_cast<double>(std::abs(y)));
        }
    }
    for (int c = nch; c < numChannels; ++c)
        std::copy(in[c], in[c] + numSamples, out[c]);
    readout_.limitDb.store(static_cast<float>(-gainToDb(deepestLimit)));
    peakHold_ = std::max(peak, peakHold_ * 0.8);
    readout_.outputPeakDb.store(static_cast<float>(gainToDb(peakHold_ + 1e-9)));
}

} // namespace mend
