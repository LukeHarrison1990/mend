// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#include "mend/Dynamics.hpp"

#include <algorithm>
#include <cmath>

namespace mend {

namespace {
double coeffFor(double seconds, double rate) { return 1.0 - std::exp(-1.0 / (seconds * rate)); }
} // namespace

// ---- DelayLines -------------------------------------------------------------

void DelayLines::prepare(int numChannels, int delay)
{
    delay_ = std::max(0, delay);
    buf_.assign(static_cast<size_t>(numChannels), std::vector<float>(static_cast<size_t>(delay_ + 1), 0.0f));
    pos_.assign(static_cast<size_t>(numChannels), 0);
}

void DelayLines::reset()
{
    for (auto& b : buf_)
        std::fill(b.begin(), b.end(), 0.0f);
    std::fill(pos_.begin(), pos_.end(), 0);
}

// ---- DeEss -------------------------------------------------------------------

void DeEss::prepare(double sampleRate, int numChannels)
{
    fs_ = sampleRate;
    nch_ = std::clamp(numChannels, 1, kMaxCh);
    ch_.assign(static_cast<size_t>(nch_), {});
    const int la = static_cast<int>(std::lround(sampleRate * 0.001));
    delay_.prepare(nch_, la);
    delayLo_.prepare(nch_, la);
    att_ = coeffFor(0.0005, sampleRate);
    rel_ = coeffFor(0.030, sampleRate);
    grAtt_ = coeffFor(0.0008, sampleRate);
    grRel_ = coeffFor(0.050, sampleRate);
    designed_ = -1.0;
    reset();
}

void DeEss::reset()
{
    for (auto& c : ch_) {
        c.lp1.reset(); c.lp2.reset(); c.hp1.reset(); c.hp2.reset();
    }
    delay_.reset();
    delayLo_.reset();
    env_ = envFull_ = grDb_ = 0.0;
}

void DeEss::design()
{
    const double f = std::clamp(settings_.frequency, 1000.0, fs_ * 0.4);
    if (std::abs(f - designed_) < 1e-6)
        return;
    const auto lp = BiquadCoeffs::lowpass(fs_, f, 0.70710678);
    const auto hp = BiquadCoeffs::highpass(fs_, f, 0.70710678);
    for (auto& c : ch_) {
        c.lp1.setCoeffs(lp); c.lp2.setCoeffs(lp);
        c.hp1.setCoeffs(hp); c.hp2.setCoeffs(hp);
    }
    designed_ = f;
}

void DeEss::process(const float* const* in, float* const* out, int numChannels, int numSamples)
{
    design();
    const int nch = std::min(numChannels, nch_);
    const double thr = settings_.thresholdDb;
    const double maxRed = std::max(0.0, settings_.maxReductionDb);
    for (int i = 0; i < numSamples; ++i) {
        double peakHi = 0.0;
        float lo[kMaxCh], hi[kMaxCh];
        for (int c = 0; c < nch; ++c) {
            Ch& ch = ch_[static_cast<size_t>(c)];
            const double x = in[c][i];
            const double l = ch.lp2.process(ch.lp1.process(x));
            const double h = ch.hp2.process(ch.hp1.process(x));
            lo[c] = static_cast<float>(l);
            hi[c] = static_cast<float>(h);
            peakHi = std::max(peakHi, h * h);
        }
        env_ += (peakHi > env_ ? att_ : rel_) * (peakHi - env_);
        const double levelDb = 10.0 * std::log10(env_ * 2.0 + 1e-20); // mean square -> sine-peak-ish dB
        // 4:1 above the threshold, limited to the maximum reduction
        const double want = std::min(maxRed, std::max(0.0, (levelDb - thr) * 0.75));
        grDb_ += (want > grDb_ ? grAtt_ : grRel_) * (want - grDb_);
        const float g = static_cast<float>(dbToGain(-grDb_));
        for (int c = 0; c < nch; ++c) {
            const float l = delayLo_.push(c, lo[c]);
            const float h = delay_.push(c, hi[c]);
            out[c][i] = settings_.wideband ? g * (l + h) : l + g * h;
        }
    }
}

// ---- Leveler -------------------------------------------------------------------

void Leveler::prepare(double sampleRate, int numChannels)
{
    fs_ = sampleRate;
    nch_ = std::max(1, numChannels);
    // BS.1770 K-weighting: +4 dB high shelf near 1.7 kHz, then a 38 Hz high-pass
    shelf_.assign(static_cast<size_t>(nch_), {});
    hp_.assign(static_cast<size_t>(nch_), {});
    BiquadCoeffs cs, ch;
    BiquadCoeffs::kWeighting(sampleRate, cs, ch);
    for (int c = 0; c < nch_; ++c) {
        shelf_[c].setCoeffs(cs);
        hp_[c].setCoeffs(ch);
    }
    const int win = static_cast<int>(sampleRate * 0.4);
    window_.assign(static_cast<size_t>(win), 0.0);
    delay_.prepare(nch_, win / 2);
    reset();
}

void Leveler::reset()
{
    for (auto& b : shelf_)
        b.reset();
    for (auto& b : hp_)
        b.reset();
    std::fill(window_.begin(), window_.end(), 0.0);
    winPos_ = 0;
    winSum_ = 0.0;
    counter_ = 0;
    gainDb_ = targetGainDb_ = 0.0;
    delay_.reset();
}

void Leveler::process(const float* const* in, float* const* out, int numChannels, int numSamples)
{
    const int nch = std::min(numChannels, nch_);
    const int update = std::max(1, static_cast<int>(fs_ * 0.005)); // retarget every 5 ms
    const double follow = coeffFor(std::max(0.05, settings_.speedSeconds), fs_ / update);
    const double perSample = coeffFor(0.010, fs_); // de-zipper the gain itself
    double g = dbToGain(gainDb_);
    for (int i = 0; i < numSamples; ++i) {
        double ms = 0.0;
        for (int c = 0; c < nch; ++c) {
            const double k = hp_[c].process(shelf_[c].process(in[c][i]));
            ms += k * k;
        }
        winSum_ += ms - window_[winPos_];
        window_[winPos_] = ms;
        if (++winPos_ == window_.size())
            winPos_ = 0;
        if (++counter_ >= update) {
            counter_ = 0;
            const double meanSq = std::max(0.0, winSum_) / static_cast<double>(window_.size());
            const double loud = -0.691 + 10.0 * std::log10(meanSq + 1e-20); // LUFS-style
            if (loud > settings_.gateDb) {
                const double want = std::clamp(settings_.targetDb - loud, -settings_.maxCutDb, settings_.maxBoostDb);
                targetGainDb_ += follow * (want - targetGainDb_);
            }
        }
        gainDb_ += perSample * (targetGainDb_ - gainDb_);
        g = dbToGain(gainDb_);
        for (int c = 0; c < nch; ++c)
            out[c][i] = static_cast<float>(delay_.push(c, in[c][i]) * g);
    }
}

// ---- BreathControl -----------------------------------------------------------

void BreathControl::prepare(double sampleRate, int numChannels)
{
    fs_ = sampleRate;
    nch_ = std::max(1, numChannels);
    n_ = 512;
    while (n_ < sampleRate * 0.02)
        n_ *= 2; // ~20 ms or more
    hop_ = n_ / 4;
    fft_.prepare(n_);
    ring_.assign(static_cast<size_t>(n_), 0.0);
    frame_.assign(static_cast<size_t>(n_), 0.0);
    win_.resize(static_cast<size_t>(n_));
    for (int i = 0; i < n_; ++i)
        win_[static_cast<size_t>(i)] = 0.5 - 0.5 * std::cos(2.0 * kPi * i / n_);
    bins_.assign(static_cast<size_t>(n_ / 2 + 1), {});
    scratch_.assign(static_cast<size_t>(n_), {});
    smoothFrames_ = std::max(3, static_cast<int>(std::lround(0.06 * sampleRate / hop_)) | 1); // ~60 ms, odd
    decisions_.assign(static_cast<size_t>(smoothFrames_), 0);
    // the decision for a frame is ready n/2 + half the smoothing window later
    delay_.prepare(nch_, n_ / 2 + (smoothFrames_ / 2) * hop_);
    att_ = coeffFor(0.008, sampleRate);
    rel_ = coeffFor(0.030, sampleRate);
    reset();
}

void BreathControl::reset()
{
    std::fill(ring_.begin(), ring_.end(), 0.0);
    std::fill(decisions_.begin(), decisions_.end(), 0);
    ringPos_ = hopCount_ = decPos_ = 0;
    speechLevelDb_ = -60.0;
    gain_ = target_ = 1.0;
    breathFrames_ = 0;
    delay_.reset();
}

bool BreathControl::classify()
{
    for (int i = 0; i < n_; ++i)
        frame_[static_cast<size_t>(i)] = ring_[static_cast<size_t>((ringPos_ + i) % n_)];
    // level
    double e = 0.0;
    for (double v : frame_)
        e += v * v;
    const double levelDb = 10.0 * std::log10(e / n_ + 1e-20);
    // voicing: normalised autocorrelation peak for 70..400 Hz
    const int lagMin = static_cast<int>(fs_ / 400.0), lagMax = std::min(n_ / 2, static_cast<int>(fs_ / 70.0));
    double best = 0.0;
    for (int lag = lagMin; lag <= lagMax; ++lag) {
        double num = 0.0, d0 = 0.0, d1 = 0.0;
        for (int i = 0; i + lag < n_; i += 2) {
            num += frame_[static_cast<size_t>(i)] * frame_[static_cast<size_t>(i + lag)];
            d0 += frame_[static_cast<size_t>(i)] * frame_[static_cast<size_t>(i)];
            d1 += frame_[static_cast<size_t>(i + lag)] * frame_[static_cast<size_t>(i + lag)];
        }
        best = std::max(best, num / std::sqrt(d0 * d1 + 1e-30));
    }
    const bool voiced = best > 0.55;
    if (voiced)
        speechLevelDb_ = std::max(speechLevelDb_, levelDb);
    speechLevelDb_ -= 6.0 * hop_ / fs_; // forget slowly (6 dB per second)
    speechLevelDb_ = std::max(speechLevelDb_, -70.0);

    // spectrum: flatness over 400 Hz..4 kHz (where breath noise lives),
    // share above 5 kHz (sibilance) of 400 Hz..8 kHz
    for (int i = 0; i < n_; ++i)
        frame_[static_cast<size_t>(i)] *= win_[static_cast<size_t>(i)];
    fft_.forwardReal(frame_.data(), bins_.data(), scratch_.data());
    const double binHz = fs_ / n_;
    const int k0 = std::max(1, static_cast<int>(400.0 / binHz));
    const int k4 = std::min(n_ / 2, static_cast<int>(4000.0 / binHz));
    const int k5 = std::min(n_ / 2, static_cast<int>(5000.0 / binHz));
    const int k8 = std::min(n_ / 2, static_cast<int>(8000.0 / binHz));
    double logSum = 0.0, sum = 0.0, all = 0.0, top = 0.0;
    for (int k = k0; k < k8; ++k) {
        const double p = std::norm(bins_[static_cast<size_t>(k)]) + 1e-24;
        if (k < k4) {
            logSum += std::log(p);
            sum += p;
        }
        all += p;
        if (k >= k5)
            top += p;
    }
    const int nb = std::max(1, k4 - k0);
    const double flatness = std::exp(logSum / nb) / (sum / nb + 1e-30);
    const double topShare = top / (all + 1e-30);

    const double sens = std::clamp(settings_.sensitivity, 0.0, 100.0) / 100.0;
    const double flatMin = 0.30 - 0.20 * sens;      // how noise-like it must be
    const double belowSpeech = 16.0 - 10.0 * sens;  // dB under the speech level
    return !voiced && flatness > flatMin && topShare < 0.6 && levelDb > -75.0
        && levelDb < speechLevelDb_ - belowSpeech;
}

void BreathControl::process(const float* const* in, float* const* out, int numChannels, int numSamples)
{
    const int nch = std::min(numChannels, nch_);
    const double floorGain = dbToGain(-std::clamp(settings_.reductionDb, 0.0, 80.0));
    for (int i = 0; i < numSamples; ++i) {
        double m = 0.0;
        for (int c = 0; c < nch; ++c)
            m += in[c][i];
        ring_[static_cast<size_t>(ringPos_)] = m / nch;
        ringPos_ = (ringPos_ + 1) % n_;
        if (++hopCount_ >= hop_) {
            hopCount_ = 0;
            decisions_[static_cast<size_t>(decPos_)] = classify() ? 1 : 0;
            decPos_ = (decPos_ + 1) % smoothFrames_;
            int votes = 0;
            for (char d : decisions_)
                votes += d;
            const bool breath = votes * 2 > smoothFrames_; // majority over ~60 ms
            breathFrames_ += breath ? 1 : 0;
            target_ = breath ? floorGain : 1.0;
        }
        gain_ += (target_ < gain_ ? att_ : rel_) * (target_ - gain_);
        for (int c = 0; c < nch; ++c)
            out[c][i] = static_cast<float>(delay_.push(c, in[c][i]) * gain_);
    }
}

} // namespace mend
