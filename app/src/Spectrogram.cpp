// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#include "Spectrogram.hpp"

#include "mend/Fft.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstring>

namespace mend::editor {

namespace {

constexpr double kLogMinHz = 20.0;
constexpr double kPi = 3.14159265358979323846;

double mel(double f) { return 2595.0 * std::log10(1.0 + f / 700.0); }
double melInv(double m) { return 700.0 * (std::pow(10.0, m / 2595.0) - 1.0); }

struct Lut {
    uint8_t rgb[1024][3];
    Lut()
    {
        // black -> deep violet -> magenta -> orange -> yellow -> near white
        static const float stops[][4] = {
            { 0.00f, 0, 0, 0 },       { 0.18f, 22, 8, 58 },     { 0.38f, 92, 20, 122 },
            { 0.58f, 190, 46, 92 },   { 0.76f, 246, 120, 34 },  { 0.90f, 252, 206, 72 },
            { 1.00f, 255, 252, 226 },
        };
        const int ns = sizeof(stops) / sizeof(stops[0]);
        for (int i = 0; i < 1024; ++i) {
            const float t = i / 1023.0f;
            int k = 0;
            while (k < ns - 2 && t > stops[k + 1][0])
                ++k;
            const float u = (t - stops[k][0]) / (stops[k + 1][0] - stops[k][0]);
            for (int c = 0; c < 3; ++c)
                rgb[i][c] = static_cast<uint8_t>(std::lround(stops[k][c + 1] + u * (stops[k + 1][c + 1] - stops[k][c + 1])));
        }
    }
};
const Lut& lut()
{
    static const Lut l;
    return l;
}

// How each output row reads the spectrum.
struct RowMap {
    int lo, hi;  // bin range to take the maximum over (inclusive), or
    float frac;  // interpolation between lo and lo+1 when hi < lo
};

std::vector<RowMap> makeRowMap(const SpecView& v, double fs, int n)
{
    const double binHz = fs / n;
    const int nb = n / 2;
    std::vector<RowMap> m(static_cast<size_t>(v.rows));
    for (int r = 0; r < v.rows; ++r) {
        const double fHi = specFreqAt(v.scale, fs, static_cast<double>(r) / v.rows);
        const double fLo = specFreqAt(v.scale, fs, static_cast<double>(r + 1) / v.rows);
        const double bLo = fLo / binHz, bHi = fHi / binHz;
        RowMap& rm = m[static_cast<size_t>(r)];
        if (bHi - bLo >= 1.0) {
            rm.lo = std::clamp(static_cast<int>(std::ceil(bLo)), 0, nb);
            rm.hi = std::clamp(static_cast<int>(std::floor(bHi)), rm.lo, nb);
            rm.frac = 0;
        } else {
            const double bc = std::clamp(0.5 * (bLo + bHi), 0.0, static_cast<double>(nb));
            rm.lo = std::min(static_cast<int>(bc), nb - 1);
            rm.hi = -1;
            rm.frac = static_cast<float>(bc - rm.lo);
        }
    }
    return m;
}

// Renders columns [c0, c1) of one channel. Returns false if cancelled.
template <typename CancelFn>
bool renderColumns(const AudioBuffer& a, int ch, const SpecView& v, const std::vector<RowMap>& rows, uint8_t* rgba,
                   int c0, int c1, CancelFn&& cancelled)
{
    const double fs = a.sampleRate;
    const int n = specFftSize(fs);
    const int nb = n / 2;
    Fft fft;
    fft.prepare(n);
    std::vector<double> win(static_cast<size_t>(n)), frame(static_cast<size_t>(n));
    std::vector<std::complex<double>> bins(static_cast<size_t>(nb + 1)), scratch(static_cast<size_t>(n));
    double sumW = 0;
    for (int i = 0; i < n; ++i) { // 4-term Blackman-Harris: low leakage
        const double x = 2.0 * kPi * i / (n - 1);
        win[static_cast<size_t>(i)] = 0.35875 - 0.48829 * std::cos(x) + 0.14128 * std::cos(2 * x) - 0.01168 * std::cos(3 * x);
        sumW += win[static_cast<size_t>(i)];
    }
    const double norm = 4.0 / (sumW * sumW); // |X|^2 -> power where a full-scale sine reads 0 dB
    std::vector<float> db(static_cast<size_t>(nb + 1));
    std::vector<double> pw(static_cast<size_t>(nb + 1));
    const float* x = a.channels[static_cast<size_t>(ch)].data();
    const long long total = static_cast<long long>(a.numFrames());
    const float range = -v.floorDb;
    const auto& L = lut();
    const int sub = v.framesPerCol > nb ? std::min(4, static_cast<int>(std::ceil(v.framesPerCol / nb))) : 1;

    for (int c = c0; c < c1; ++c) {
        if ((c & 15) == 0 && cancelled())
            return false;
        const double start = v.viewStart + c * v.framesPerCol;
        const bool outside = start + v.framesPerCol <= 0 || start >= static_cast<double>(total);
        if (outside) {
            for (int r = 0; r < v.rows; ++r) {
                uint8_t* p = rgba + (static_cast<size_t>(r) * v.cols + c) * 4;
                p[0] = 10, p[1] = 11, p[2] = 14, p[3] = 255;
            }
            continue;
        }
        std::fill(pw.begin(), pw.end(), 0.0);
        for (int s = 0; s < sub; ++s) {
            const double centre = start + (s + 0.5) * v.framesPerCol / sub;
            const long long f0 = static_cast<long long>(std::floor(centre)) - nb;
            for (int i = 0; i < n; ++i) {
                const long long f = f0 + i;
                frame[static_cast<size_t>(i)] = (f >= 0 && f < total) ? x[f] * win[static_cast<size_t>(i)] : 0.0;
            }
            fft.forwardReal(frame.data(), bins.data(), scratch.data());
            for (int k = 0; k <= nb; ++k)
                pw[static_cast<size_t>(k)] = std::max(pw[static_cast<size_t>(k)], std::norm(bins[static_cast<size_t>(k)]) * norm);
        }
        for (int k = 0; k <= nb; ++k)
            db[static_cast<size_t>(k)] = static_cast<float>(10.0 * std::log10(pw[static_cast<size_t>(k)] + 1e-24));
        for (int r = 0; r < v.rows; ++r) {
            const RowMap& m = rows[static_cast<size_t>(r)];
            float d;
            if (m.hi >= m.lo) {
                d = db[static_cast<size_t>(m.lo)];
                for (int k = m.lo + 1; k <= m.hi; ++k)
                    d = std::max(d, db[static_cast<size_t>(k)]);
            } else {
                d = db[static_cast<size_t>(m.lo)] + m.frac * (db[static_cast<size_t>(m.lo + 1)] - db[static_cast<size_t>(m.lo)]);
            }
            const float t = std::clamp((d - v.floorDb) / range, 0.0f, 1.0f);
            const auto& col = L.rgb[static_cast<int>(t * 1023.0f + 0.5f)];
            uint8_t* p = rgba + (static_cast<size_t>(r) * v.cols + c) * 4;
            p[0] = col[0], p[1] = col[1], p[2] = col[2], p[3] = 255;
        }
    }
    return true;
}

} // namespace

int specFftSize(double fs)
{
    const double want = fs * 0.0465;
    int n = 256;
    while (n < 16384 && n * 1.5 < want)
        n *= 2;
    return n;
}

double specFreqAt(FreqScale s, double fs, double pos)
{
    const double nyq = fs * 0.5;
    const double u = 1.0 - std::clamp(pos, 0.0, 1.0);
    switch (s) {
    case FreqScale::Linear: return u * nyq;
    case FreqScale::Mel: return melInv(u * mel(nyq));
    case FreqScale::Log: return kLogMinHz * std::pow(nyq / kLogMinHz, u);
    }
    return 0.0;
}

double specPosOf(FreqScale s, double fs, double hz)
{
    const double nyq = fs * 0.5;
    double u = 0;
    switch (s) {
    case FreqScale::Linear: u = hz / nyq; break;
    case FreqScale::Mel: u = mel(hz) / mel(nyq); break;
    case FreqScale::Log: u = hz <= kLogMinHz ? 0.0 : std::log(hz / kLogMinHz) / std::log(nyq / kLogMinHz); break;
    }
    return 1.0 - std::clamp(u, 0.0, 1.0);
}

void specColour(float t, uint8_t& r, uint8_t& g, uint8_t& b)
{
    const auto& c = lut().rgb[static_cast<int>(std::clamp(t, 0.0f, 1.0f) * 1023.0f + 0.5f)];
    r = c[0], g = c[1], b = c[2];
}

void renderSpectrogram(const AudioBuffer& a, int channel, const SpecView& v, std::vector<uint8_t>& rgba)
{
    rgba.assign(static_cast<size_t>(v.cols) * v.rows * 4, 0);
    const auto rows = makeRowMap(v, a.sampleRate, specFftSize(a.sampleRate));
    renderColumns(a, channel, v, rows, rgba.data(), 0, v.cols, [] { return false; });
}

void rasterWaveformAlpha(const AudioBuffer& a, const PeakCache& peaks, int channel, double viewStart,
                         double framesPerCol, int w, int h, std::vector<uint8_t>& alpha)
{
    const size_t bytes = static_cast<size_t>(w) * h;
    if (alpha.size() != bytes)
        alpha.resize(bytes);
    std::memset(alpha.data(), 0, bytes);
    const double n = static_cast<double>(a.numFrames());
    const float mid = h * 0.5f, amp = h * 0.47f;
    for (int i = 0; i < w; ++i) {
        const double f0 = viewStart + i * framesPerCol, f1 = f0 + framesPerCol;
        if (f1 <= 0 || f0 >= n)
            continue;
        size_t from = static_cast<size_t>(std::max(0.0, std::floor(f0)));
        const size_t to = std::max(from + 1, static_cast<size_t>(std::ceil(std::min(f1, n))));
        if (framesPerCol < 16 && from > 0)
            --from; // join to the previous column so steep slopes stay connected
        float mn, mx;
        peaks.range(a, channel, from, to, mn, mx);
        float y0 = mid - std::clamp(mx, -1.0f, 1.0f) * amp;
        float y1 = mid - std::clamp(mn, -1.0f, 1.0f) * amp;
        if (y1 - y0 < 1.0f) {
            const float c = 0.5f * (y0 + y1);
            y0 = c - 0.5f;
            y1 = c + 0.5f;
        }
        const int ys = std::max(0, static_cast<int>(std::floor(y0)));
        const int ye = std::min(h, static_cast<int>(std::ceil(y1)));
        uint8_t* col = alpha.data() + i;
        for (int y = ys; y < ye; ++y) {
            const float cov = std::min(static_cast<float>(y + 1), y1) - std::max(static_cast<float>(y), y0);
            col[static_cast<size_t>(y) * w] = static_cast<uint8_t>(std::clamp(cov, 0.0f, 1.0f) * 255.0f + 0.5f);
        }
    }
}

void rasterWaveform(const AudioBuffer& a, const PeakCache& peaks, int channel, double viewStart, double framesPerCol,
                    int w, int h, uint8_t r, uint8_t g, uint8_t b, std::vector<uint8_t>& rgba)
{
    std::vector<uint8_t> alpha;
    rasterWaveformAlpha(a, peaks, channel, viewStart, framesPerCol, w, h, alpha);
    rgba.resize(alpha.size() * 4);
    for (size_t i = 0; i < alpha.size(); ++i) {
        uint8_t* p = rgba.data() + i * 4;
        p[0] = r, p[1] = g, p[2] = b, p[3] = alpha[i];
    }
}

// ---------------------------------------------------------------------------

// No condition variables: the UI thread polls (it redraws while a
// spectrogram is pending anyway), which keeps this simple and portable.
SpectrogramWorker::~SpectrogramWorker()
{
    quiesce();
}

void SpectrogramWorker::request(const AudioBuffer* audio, const SpecView& v)
{
    audio_ = audio;
    next_ = v;
    pending_ = true;
    ++generation_; // cancels the job in progress, if any
    pump();
}

void SpectrogramWorker::pump()
{
    if (!pending_ || running_.load())
        return;
    pending_ = false;
    running_ = true;
    const uint64_t gen = generation_.load();
    // Detached: completion is signalled through running_, and quiesce()
    // waits on that. (Joining from the UI thread trips a MinGW/winpthreads
    // thread-id clash on some systems.)
    std::thread([this, a = audio_, v = next_, gen] { run(a, v, gen); }).detach();
}

bool SpectrogramWorker::take(SpecView& v, std::vector<std::vector<uint8_t>>& lanes)
{
    pump();
    // the worker only writes the result while running_ is set, and sets
    // ready_ after writing; we only read when it's ready and idle
    if (running_.load() || !ready_.load())
        return false;
    v = resultView_;
    lanes.swap(result_);
    ready_ = false;
    return true;
}

void SpectrogramWorker::quiesce()
{
    pending_ = false;
    ++generation_;
    while (running_.load()) // the job notices the new generation within a few columns
        std::this_thread::yield();
    ready_ = false;
}

void SpectrogramWorker::run(const AudioBuffer* audio, SpecView v, uint64_t gen)
{
    const auto cancelled = [this, gen] { return generation_.load() != gen; };
    bool ok = audio != nullptr && v.cols > 0 && v.rows > 0;
    std::vector<std::vector<uint8_t>> lanes;
    if (ok) {
        const int nch = audio->numChannels();
        const auto rows = makeRowMap(v, audio->sampleRate, specFftSize(audio->sampleRate));
        lanes.resize(static_cast<size_t>(nch));
        for (auto& lane : lanes)
            lane.resize(static_cast<size_t>(v.cols) * v.rows * 4);
        const int hw = static_cast<int>(std::thread::hardware_concurrency());
        const int nthreads = std::clamp(hw - 1, 1, 8);
        std::atomic<bool> allOk { true };
        const auto work = [&](int t) {
            for (int ch = 0; ch < nch; ++ch) {
                const int c0 = v.cols * t / nthreads, c1 = v.cols * (t + 1) / nthreads;
                if (!renderColumns(*audio, ch, v, rows, lanes[static_cast<size_t>(ch)].data(), c0, c1, cancelled)) {
                    allOk = false;
                    return;
                }
            }
        };
        // helpers are detached and counted rather than joined (see pump())
        std::atomic<int> remaining { nthreads - 1 };
        for (int t = 1; t < nthreads; ++t)
            std::thread([&work, &remaining, t] {
                work(t);
                --remaining;
            }).detach();
        work(0);
        while (remaining.load() > 0)
            std::this_thread::yield();
        ok = allOk.load() && !cancelled();
    }
    if (ok) {
        resultView_ = v;
        result_.swap(lanes);
        ready_ = true;
    }
    running_ = false;
}

} // namespace mend::editor
