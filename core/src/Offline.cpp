// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#include "mend/Offline.hpp"

#include "mend/ArModel.hpp"
#include "mend/Biquad.hpp"
#include "mend/Fft.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numeric>
#include <random>

namespace mend::offline {

namespace {

using cd = std::complex<double>;

int nextPow2(size_t v)
{
    int n = 1;
    while (static_cast<size_t>(n) < v)
        n *= 2;
    return n;
}

double sinc(double x) { return std::abs(x) < 1e-12 ? 1.0 : std::sin(kPi * x) / (kPi * x); }

// Kaiser window value for u in [-1, 1].
double kaiser(double u, double beta)
{
    auto i0 = [](double x) {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 40; ++k) {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;
        }
        return sum;
    };
    if (std::abs(u) >= 1.0)
        return 0.0;
    return i0(beta * std::sqrt(1.0 - u * u)) / i0(beta);
}

// Streaming offline STFT: for each frame, call fn(frameIndex, centreSample,
// bins per channel); overlap-adds the result into the returned buffers.
// Hann analysis and synthesis, 75% overlap.
template <typename Fn>
Channels stftProcess(const Channels& x, int n, Fn&& fn)
{
    const int nch = static_cast<int>(x.size());
    const long long len = static_cast<long long>(frames(x));
    const int hop = n / 4;
    Fft fft;
    fft.prepare(n);
    std::vector<double> win(static_cast<size_t>(n)), frame(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        win[static_cast<size_t>(i)] = 0.5 - 0.5 * std::cos(2.0 * kPi * i / n);
    const double norm = 1.0 / 1.5; // sum of hann^2 at 75% overlap
    std::vector<std::vector<cd>> bins(static_cast<size_t>(nch), std::vector<cd>(static_cast<size_t>(n / 2 + 1)));
    std::vector<cd> scratch(static_cast<size_t>(n));
    std::vector<std::vector<double>> acc(static_cast<size_t>(nch), std::vector<double>(static_cast<size_t>(len), 0.0));
    long long f = 0;
    for (long long start = -n + hop; start < len; start += hop, ++f) {
        for (int c = 0; c < nch; ++c) {
            const float* xs = x[static_cast<size_t>(c)].data();
            for (int i = 0; i < n; ++i) {
                const long long k = start + i;
                frame[static_cast<size_t>(i)] = (k >= 0 && k < len) ? xs[k] * win[static_cast<size_t>(i)] : 0.0;
            }
            fft.forwardReal(frame.data(), bins[static_cast<size_t>(c)].data(), scratch.data());
        }
        fn(f, start + n / 2, bins);
        for (int c = 0; c < nch; ++c) {
            fft.inverseReal(bins[static_cast<size_t>(c)].data(), frame.data(), scratch.data());
            auto& a = acc[static_cast<size_t>(c)];
            for (int i = 0; i < n; ++i) {
                const long long k = start + i;
                if (k >= 0 && k < len)
                    a[static_cast<size_t>(k)] += frame[static_cast<size_t>(i)] * win[static_cast<size_t>(i)] * norm;
            }
        }
    }
    Channels y(static_cast<size_t>(nch));
    for (int c = 0; c < nch; ++c)
        y[static_cast<size_t>(c)].assign(acc[static_cast<size_t>(c)].begin(), acc[static_cast<size_t>(c)].end());
    return y;
}

double smoothstep(double t)
{
    t = std::clamp(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// Weight for bin k inside [kLo, kHi] with soft edges.
double bandWeight(double k, double kLo, double kHi)
{
    if (k < kLo || k > kHi)
        return 0.0;
    const double taper = std::max(2.0, 0.08 * (kHi - kLo));
    return smoothstep((k - kLo) / taper + 0.5) * smoothstep((kHi - k) / taper + 0.5);
}

// K-weighting filters (BS.1770) for one channel, applied to a copy.
std::vector<double> kWeighted(const std::vector<float>& x, double fs)
{
    BiquadCoeffs cs, ch;
    BiquadCoeffs::kWeighting(fs, cs, ch);
    Biquad shelf, hp;
    shelf.setCoeffs(cs);
    hp.setCoeffs(ch);
    std::vector<double> y(x.size());
    for (size_t i = 0; i < x.size(); ++i)
        y[i] = hp.process(shelf.process(x[i]));
    return y;
}

// 4x oversampling interpolation filter (polyphase, 48 taps per phase).
struct Oversampler {
    static constexpr int kTaps = 48, kPhases = 4;
    double h[kPhases][kTaps];
    Oversampler()
    {
        for (int p = 0; p < kPhases; ++p)
            for (int t = 0; t < kTaps; ++t) {
                const double pos = (t - kTaps / 2 + 1) - static_cast<double>(p) / kPhases; // in input samples
                h[p][t] = sinc(pos * 0.98) * 0.98 * kaiser(pos / (kTaps / 2), 8.0);
            }
    }
    // Largest |value| among the 4 sub-sample points in [i, i+1).
    double peakAt(const std::vector<float>& x, size_t i) const
    {
        double m = std::abs(x[i]);
        for (int p = 1; p < kPhases; ++p) {
            double v = 0.0;
            for (int t = 0; t < kTaps; ++t) {
                const long long k = static_cast<long long>(i) + 1 + t - kTaps / 2;
                if (k >= 0 && k < static_cast<long long>(x.size()))
                    v += x[static_cast<size_t>(k)] * h[kPhases - p][t];
            }
            m = std::max(m, std::abs(v));
        }
        return m;
    }
};

const Oversampler& oversampler()
{
    static const Oversampler o;
    return o;
}

// Hilbert transform (FIR, 4095 taps), aligned with the input.
std::vector<float> hilbert(const std::vector<float>& x)
{
    const int L = 4095, c = L / 2;
    std::vector<double> h(L, 0.0);
    for (int i = 0; i < L; ++i) {
        const int m = i - c;
        if (m % 2 != 0) {
            const double w = 0.42 - 0.5 * std::cos(2 * kPi * i / (L - 1)) + 0.08 * std::cos(4 * kPi * i / (L - 1));
            h[static_cast<size_t>(i)] = 2.0 / (kPi * m) * w;
        }
    }
    std::vector<float> y = x;
    convolveCentered(y, h);
    return y;
}

} // namespace

// ---- level ------------------------------------------------------------------

void applyGain(Channels& x, double db)
{
    const float g = static_cast<float>(dbToGain(db));
    for (auto& c : x)
        for (auto& v : c)
            v *= g;
}

double samplePeak(const Channels& x)
{
    double m = 0.0;
    for (const auto& c : x)
        for (float v : c)
            m = std::max(m, static_cast<double>(std::abs(v)));
    return m;
}

double normalizePeak(Channels& x, double targetDb, bool perChannel)
{
    const double target = dbToGain(targetDb);
    double applied = -200.0;
    if (perChannel) {
        for (auto& c : x) {
            double m = 0.0;
            for (float v : c)
                m = std::max(m, static_cast<double>(std::abs(v)));
            if (m <= 0.0)
                continue;
            const float g = static_cast<float>(target / m);
            for (auto& v : c)
                v *= g;
            applied = std::max(applied, gainToDb(target / m));
        }
        return applied;
    }
    const double m = samplePeak(x);
    if (m <= 0.0)
        return 0.0;
    applyGain(x, gainToDb(target / m));
    return gainToDb(target / m);
}

void applyFade(Channels& x, bool fadeIn, FadeCurve curve)
{
    const size_t n = frames(x);
    if (n < 2)
        return;
    for (size_t i = 0; i < n; ++i) {
        double t = static_cast<double>(i) / (n - 1);
        if (!fadeIn)
            t = 1.0 - t;
        double g = t;
        switch (curve) {
        case FadeCurve::Linear: g = t; break;
        case FadeCurve::EqualPower: g = std::sin(0.5 * kPi * t); break;
        case FadeCurve::SCurve: g = 0.5 - 0.5 * std::cos(kPi * t); break;
        case FadeCurve::Exponential: g = t <= 0 ? 0.0 : dbToGain(60.0 * (t - 1.0)) * t; break; // slow start
        case FadeCurve::Logarithmic: g = 1.0 - (1.0 - t) * (1.0 - t); break;                // fast start
        }
        for (auto& c : x)
            c[i] = static_cast<float>(c[i] * g);
    }
}

// ---- phase and channels -----------------------------------------------------------

void removeDc(Channels& x)
{
    for (auto& c : x) {
        if (c.empty())
            continue;
        const double mean = std::accumulate(c.begin(), c.end(), 0.0) / static_cast<double>(c.size());
        for (auto& v : c)
            v = static_cast<float>(v - mean);
    }
}

void invertPolarity(Channels& x, int channel)
{
    for (size_t c = 0; c < x.size(); ++c)
        if (channel < 0 || static_cast<size_t>(channel) == c)
            for (auto& v : x[c])
                v = -v;
}

void rotatePhase(Channels& x, double degrees)
{
    const double th = degrees * kPi / 180.0;
    const float cs = static_cast<float>(std::cos(th)), sn = static_cast<float>(std::sin(th));
    for (auto& c : x) {
        const auto h = hilbert(c);
        for (size_t i = 0; i < c.size(); ++i)
            c[i] = cs * c[i] - sn * h[i]; // sin(wt) -> sin(wt + theta)
    }
}

double bestPhaseRotation(const Channels& x)
{
    std::vector<std::vector<float>> h;
    for (const auto& c : x)
        h.push_back(hilbert(c));
    double best = 0.0, bestPeak = 1e30;
    for (int d = 0; d < 180; d += 5) {
        const double th = d * kPi / 180.0, cs = std::cos(th), sn = std::sin(th);
        double peak = 0.0;
        for (size_t c = 0; c < x.size(); ++c) {
            // the Hilbert filter rings at the very edges; judge the middle
            const size_t edge = x[c].size() > 3 * 2048 ? 2048 : 0;
            for (size_t i = edge; i + edge < x[c].size(); ++i)
                peak = std::max(peak, std::abs(cs * x[c][i] - sn * h[c][i]));
        }
        if (peak < bestPeak - 1e-9) {
            bestPeak = peak;
            best = d;
        }
    }
    return best;
}

void mixOp(Channels& x, MixOp op)
{
    if (x.size() < 2)
        return;
    auto& L = x[0];
    auto& R = x[1];
    switch (op) {
    case MixOp::SwapLR: L.swap(R); break;
    case MixOp::MonoSum:
        for (size_t i = 0; i < L.size(); ++i)
            L[i] = R[i] = 0.5f * (L[i] + R[i]);
        break;
    case MixOp::MidSideEncode:
        for (size_t i = 0; i < L.size(); ++i) {
            const float m = 0.5f * (L[i] + R[i]), s = 0.5f * (L[i] - R[i]);
            L[i] = m;
            R[i] = s;
        }
        break;
    case MixOp::MidSideDecode:
        for (size_t i = 0; i < L.size(); ++i) {
            const float l = L[i] + R[i], r = L[i] - R[i];
            L[i] = l;
            R[i] = r;
        }
        break;
    }
}

void channelGains(Channels& x, double leftDb, double rightDb)
{
    if (!x.empty()) {
        const float g = static_cast<float>(dbToGain(leftDb));
        for (auto& v : x[0])
            v *= g;
    }
    if (x.size() > 1) {
        const float g = static_cast<float>(dbToGain(rightDb));
        for (auto& v : x[1])
            v *= g;
    }
}

Azimuth measureAzimuth(const Channels& x, double fs, double maxDelayMs)
{
    Azimuth a;
    if (x.size() < 2 || frames(x) < 64)
        return a;
    const auto& L = x[0];
    const auto& R = x[1];
    const size_t n = L.size();
    // up to 10 s from the middle
    const size_t len = std::min(n, static_cast<size_t>(fs * 10));
    const size_t s0 = (n - len) / 2;
    const int D = std::max(1, static_cast<int>(fs * maxDelayMs / 1000.0));
    double el = 0, er = 0;
    for (size_t i = s0; i < s0 + len; ++i) {
        el += static_cast<double>(L[i]) * L[i];
        er += static_cast<double>(R[i]) * R[i];
    }
    if (el <= 0 || er <= 0)
        return a;
    std::vector<double> corr(static_cast<size_t>(2 * D + 1), 0.0);
    for (int d = -D; d <= D; ++d) {
        double s = 0.0;
        for (size_t i = s0; i < s0 + len; ++i) {
            const long long j = static_cast<long long>(i) + d;
            if (j >= 0 && j < static_cast<long long>(n))
                s += static_cast<double>(L[i]) * R[static_cast<size_t>(j)];
        }
        corr[static_cast<size_t>(d + D)] = s / std::sqrt(el * er);
    }
    int best = 0;
    for (int i = 1; i < 2 * D + 1; ++i)
        if (corr[static_cast<size_t>(i)] > corr[static_cast<size_t>(best)])
            best = i;
    // sub-sample peak: band-limited (sinc) interpolation of the correlation
    auto at = [&](double pos) {
        double v = 0.0;
        for (int m = std::max(0, static_cast<int>(pos) - 16); m <= std::min(2 * D, static_cast<int>(pos) + 16); ++m)
            v += corr[static_cast<size_t>(m)] * sinc(pos - m);
        return v;
    };
    double bestPos = best, bestVal = corr[static_cast<size_t>(best)];
    for (double step : { 0.1, 0.01, 0.001 }) {
        const double centre = bestPos;
        for (int k = -10; k <= 10; ++k) {
            const double p = centre + k * step;
            if (p < 0 || p > 2 * D)
                continue;
            const double v = at(p);
            if (v > bestVal) {
                bestVal = v;
                bestPos = p;
            }
        }
    }
    a.delaySamples = bestPos - D;
    a.correlation = std::clamp(bestVal, 0.0, 1.0);
    a.gainDb = 10.0 * std::log10(el / er);
    return a;
}

void correctAzimuth(Channels& x, double delaySamples, double gainDb)
{
    if (x.size() < 2)
        return;
    auto& R = x[1];
    const std::vector<float> src = R;
    const long long n = static_cast<long long>(src.size());
    const int W = 32;
    const float g = static_cast<float>(dbToGain(gainDb));
    for (long long i = 0; i < n; ++i) {
        const double t = i + delaySamples; // read R from where it really is
        const long long k0 = static_cast<long long>(std::floor(t));
        double v = 0.0;
        for (long long k = k0 - W + 1; k <= k0 + W; ++k)
            if (k >= 0 && k < n)
                v += src[static_cast<size_t>(k)] * sinc(t - k) * kaiser((t - k) / W, 7.0);
        R[static_cast<size_t>(i)] = static_cast<float>(v) * g;
    }
}

// ---- repair ------------------------------------------------------------------------

bool interpolateGap(Channels& x, size_t gapStart, size_t gapLen)
{
    const size_t n = frames(x);
    if (gapLen == 0 || gapStart + gapLen > n)
        return false;
    const size_t before = gapStart, after = n - gapStart - gapLen;
    const size_t ctx = std::min(before, after);
    const int order = static_cast<int>(std::min<size_t>(64, std::max<size_t>(4, ctx / 3)));
    if (before < static_cast<size_t>(order) + 1 || after < static_cast<size_t>(order) + 1)
        return false;
    const size_t fitLen = std::min<size_t>(std::max<size_t>(before, after), std::max<size_t>(4096, gapLen * 4));
    for (auto& c : x) {
        // fit on the longer side, next to the gap
        std::vector<double> d(c.begin(), c.end());
        ArModel ar;
        ar.prepare(order, static_cast<int>(gapLen), static_cast<int>(fitLen));
        const bool usedBefore = before >= after;
        const double* fitStart = usedBefore ? d.data() + (gapStart - std::min(before, fitLen))
                                            : d.data() + gapStart + gapLen;
        const int fl = static_cast<int>(usedBefore ? std::min(before, fitLen) : std::min(after, fitLen));
        if (!ar.fit(fitStart, fl, order))
            continue; // silent: nothing sensible to do but leave it
        // interpolate on a window around the gap
        const size_t w0 = gapStart - static_cast<size_t>(ar.order());
        const size_t w1 = gapStart + gapLen + static_cast<size_t>(ar.order());
        std::vector<double> seg(d.begin() + static_cast<long long>(w0), d.begin() + static_cast<long long>(w1));
        ar.interpolate(seg.data(), ar.order(), ar.order() + static_cast<int>(gapLen));
        for (size_t i = 0; i < gapLen; ++i)
            c[gapStart + i] = static_cast<float>(seg[static_cast<size_t>(ar.order()) + i]);
    }
    return true;
}

void generate(Channels& x, double fs, SignalType type, double freq, double levelDb, unsigned seed)
{
    const size_t n = frames(x);
    const double amp = dbToGain(levelDb);
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> uni(-1.0, 1.0);
    const size_t fade = std::min<size_t>(n / 2, static_cast<size_t>(fs * 0.005));
    for (size_t c = 0; c < x.size(); ++c) {
        double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0; // pink filter state
        for (size_t i = 0; i < n; ++i) {
            double v = 0.0;
            switch (type) {
            case SignalType::Silence: v = 0.0; break;
            case SignalType::Sine: v = amp * std::sin(2 * kPi * freq * i / fs); break;
            case SignalType::Bleep: v = amp * std::sin(2 * kPi * 1000.0 * i / fs); break;
            case SignalType::WhiteNoise: v = amp * uni(rng) * 1.7320508; break; // unit RMS uniform
            case SignalType::PinkNoise: { // Paul Kellet's filter
                const double w = uni(rng);
                b0 = 0.99886 * b0 + w * 0.0555179;
                b1 = 0.99332 * b1 + w * 0.0750759;
                b2 = 0.96900 * b2 + w * 0.1538520;
                b3 = 0.86650 * b3 + w * 0.3104856;
                b4 = 0.55000 * b4 + w * 0.5329522;
                b5 = -0.7616 * b5 - w * 0.0168980;
                v = amp * 0.11 * (b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362) * 1.7320508;
                b6 = w * 0.115926;
                break;
            }
            }
            if (type != SignalType::Silence && fade > 0) { // no clicks at the edges
                if (i < fade)
                    v *= static_cast<double>(i) / fade;
                else if (i >= n - fade)
                    v *= static_cast<double>(n - 1 - i) / fade;
            }
            x[c][i] = static_cast<float>(v);
        }
    }
}

// ---- loudness ----------------------------------------------------------------------

Loudness measureLoudness(const Channels& x, double fs)
{
    Loudness out;
    const size_t n = frames(x);
    if (n == 0)
        return out;
    // prefix sums of weighted K-filtered power (LFE excluded and surrounds
    // weighted in 5.1, otherwise all channels 1.0)
    std::vector<double> prefix(n + 1, 0.0);
    for (size_t c = 0; c < x.size(); ++c) {
        double gw = 1.0;
        if (x.size() >= 6) {
            if (c == 3)
                continue;
            if (c >= 4)
                gw = 1.41;
        }
        const auto k = kWeighted(x[c], fs);
        double run = 0.0;
        for (size_t i = 0; i < n; ++i) {
            run += gw * k[i] * k[i];
            prefix[i + 1] += run;
        }
    }
    auto meanSq = [&](size_t a, size_t b) { return (prefix[b] - prefix[a]) / static_cast<double>(b - a); };
    auto lufs = [](double ms) { return -0.691 + 10.0 * std::log10(ms + 1e-30); };

    // integrated: 400 ms blocks, 100 ms step
    const size_t blk = static_cast<size_t>(fs * 0.4), step = static_cast<size_t>(fs * 0.1);
    std::vector<double> blocks;
    if (n >= blk)
        for (size_t s = 0; s + blk <= n; s += step)
            blocks.push_back(meanSq(s, s + blk));
    else
        blocks.push_back(meanSq(0, n));
    std::vector<double> gated;
    for (double z : blocks)
        if (lufs(z) > -70.0)
            gated.push_back(z);
    if (!gated.empty()) {
        const double rel = lufs(std::accumulate(gated.begin(), gated.end(), 0.0) / gated.size()) - 10.0;
        double sum = 0;
        size_t cnt = 0;
        for (double z : gated)
            if (lufs(z) > rel) {
                sum += z;
                ++cnt;
            }
        if (cnt > 0)
            out.integrated = lufs(sum / cnt);
    }
    // loudness range: 3 s short-term, 100 ms step
    const size_t st = static_cast<size_t>(fs * 3.0);
    std::vector<double> sts;
    if (n >= st)
        for (size_t s = 0; s + st <= n; s += step)
            sts.push_back(meanSq(s, s + st));
    for (double z : sts)
        out.shortTermMax = std::max(out.shortTermMax, lufs(z));
    std::vector<double> stg;
    for (double z : sts)
        if (lufs(z) > -70.0)
            stg.push_back(z);
    if (stg.size() >= 2) {
        const double rel = lufs(std::accumulate(stg.begin(), stg.end(), 0.0) / stg.size()) - 20.0;
        std::vector<double> l;
        for (double z : stg)
            if (lufs(z) > rel)
                l.push_back(lufs(z));
        if (l.size() >= 2) {
            std::sort(l.begin(), l.end());
            const auto pct = [&](double p) { return l[static_cast<size_t>(std::lround(p * (l.size() - 1)))]; };
            out.range = pct(0.95) - pct(0.10);
        }
    }
    out.samplePeakDb = gainToDb(samplePeak(x));
    out.truePeakDb = truePeakDb(x, fs);
    return out;
}

double truePeakDb(const Channels& x, double)
{
    const auto& os = oversampler();
    double m = 0.0;
    for (const auto& c : x)
        for (size_t i = 0; i < c.size(); ++i)
            if (std::abs(c[i]) > m * 0.6) // only check around samples that could be the peak
                m = std::max(m, os.peakAt(c, i));
    return gainToDb(m);
}

void limitPeaks(Channels& x, double fs, double ceilingDb)
{
    const size_t n = frames(x);
    if (n == 0)
        return;
    const auto& os = oversampler();
    for (int pass = 0; pass < 3; ++pass) {
        const double ceiling = dbToGain(ceilingDb - 0.05 * pass);
        std::vector<double> req(n, 1.0);
        bool any = false;
        for (const auto& c : x)
            for (size_t i = 0; i < n; ++i)
                if (std::abs(c[i]) > ceiling * 0.5) {
                    const double p = os.peakAt(c, i);
                    if (p > ceiling) {
                        req[i] = std::min(req[i], ceiling / p);
                        any = true;
                    }
                }
        if (!any)
            return;
        // minimum over +/- L, then a moving average over +/- L: never above
        // what any sample needs, and smooth (look-ahead attack)
        const size_t L = std::max<size_t>(1, static_cast<size_t>(fs * 0.0015));
        std::vector<double> mn(n);
        { // sliding minimum (monotonic deque)
            std::vector<size_t> dq;
            size_t head = 0;
            for (size_t i = 0; i < n + L; ++i) {
                if (i < n) {
                    while (dq.size() > head && req[dq.back()] >= req[i])
                        dq.pop_back();
                    dq.push_back(i);
                }
                if (i >= L) {
                    const size_t centre = i - L;
                    while (dq[head] + L < centre)
                        ++head;
                    mn[centre] = req[dq[head]];
                }
            }
        }
        std::vector<double> g(n);
        double run = 0.0;
        std::vector<double> pre(n + 1, 0.0);
        for (size_t i = 0; i < n; ++i)
            pre[i + 1] = pre[i] + mn[i];
        for (size_t i = 0; i < n; ++i) {
            const size_t a = i >= L ? i - L : 0, b = std::min(n, i + L + 1);
            g[i] = (pre[b] - pre[a]) / static_cast<double>(b - a);
        }
        // release: gain recovers over ~80 ms, never faster than allowed
        const double rel = 1.0 - std::exp(-1.0 / (0.08 * fs));
        run = g[0];
        for (size_t i = 0; i < n; ++i) {
            run = std::min(g[i], run + rel * (1.0 - run));
            g[i] = run;
        }
        for (auto& c : x)
            for (size_t i = 0; i < n; ++i)
                c[i] = static_cast<float>(c[i] * g[i]);
    }
}

// ---- time and rate --------------------------------------------------------------------

Channels resample(const Channels& x, double fsIn, double fsOut, const Progress& progress)
{
    const size_t n = frames(x);
    if (n == 0 || fsIn <= 0 || fsOut <= 0 || fsIn == fsOut)
        return x;
    const double r = fsOut / fsIn;
    const double fc = std::min(1.0, r) * 0.96;
    const int zeros = 32;
    const int W = static_cast<int>(std::ceil(zeros / fc));
    const int sub = 512;
    // kernel table over [-W, W] in input samples, `sub` points per sample
    std::vector<double> table(static_cast<size_t>(2 * W * sub + 2));
    for (size_t i = 0; i < table.size(); ++i) {
        const double u = static_cast<double>(i) / sub - W;
        table[i] = fc * sinc(fc * u) * kaiser(u / W, 9.0);
    }
    const size_t m = static_cast<size_t>(std::llround(static_cast<double>(n) * r));
    Channels y(x.size(), std::vector<float>(m));
    for (size_t j = 0; j < m; ++j) {
        if (progress && (j & 0xFFFF) == 0 && !progress(static_cast<double>(j) / m))
            return {};
        const double t = j / r;
        const long long k0 = static_cast<long long>(std::floor(t));
        for (size_t c = 0; c < x.size(); ++c) {
            const float* xs = x[c].data();
            double v = 0.0;
            for (long long k = k0 - W + 1; k <= k0 + W; ++k) {
                if (k < 0 || k >= static_cast<long long>(n))
                    continue;
                const double pos = (t - k + W) * sub;
                const size_t i0 = static_cast<size_t>(pos);
                const double fr = pos - i0;
                v += xs[k] * (table[i0] + fr * (table[i0 + 1] - table[i0]));
            }
            y[c][j] = static_cast<float>(v);
        }
    }
    return y;
}

Channels trimSilence(const Channels& x, double fs, double thresholdDb, double minSilenceSec, double keepSec,
                     size_t* removed)
{
    const size_t n = frames(x);
    const size_t hop = std::max<size_t>(1, static_cast<size_t>(fs * 0.01));
    const double thr = dbToGain(thresholdDb);
    const size_t nf = (n + hop - 1) / hop;
    std::vector<char> silent(nf, 0);
    for (size_t f = 0; f < nf; ++f) {
        double ms = 0.0;
        const size_t a = f * hop, b = std::min(n, a + hop);
        for (const auto& c : x)
            for (size_t i = a; i < b; ++i)
                ms = std::max(ms, static_cast<double>(c[i]) * c[i]);
        silent[f] = std::sqrt(ms) < thr; // the frame's peak is under the threshold
    }
    const size_t minLen = static_cast<size_t>(minSilenceSec * fs), keep = static_cast<size_t>(keepSec * fs);
    std::vector<std::pair<size_t, size_t>> cuts; // [a, b) to remove
    for (size_t f = 0; f < nf;) {
        if (!silent[f]) {
            ++f;
            continue;
        }
        size_t g = f;
        while (g < nf && silent[g])
            ++g;
        const size_t a = f * hop, b = std::min(n, g * hop);
        if (b - a >= minLen && b - a > keep)
            cuts.push_back({ a + keep / 2, b - (keep - keep / 2) });
        f = g;
    }
    size_t total = 0;
    for (auto& c : cuts)
        total += c.second - c.first;
    if (removed)
        *removed = total;
    Channels y(x.size());
    for (size_t c = 0; c < x.size(); ++c) {
        y[c].reserve(n - total);
        size_t pos = 0;
        for (auto& cut : cuts) {
            y[c].insert(y[c].end(), x[c].begin() + static_cast<long long>(pos), x[c].begin() + static_cast<long long>(cut.first));
            pos = cut.second;
        }
        y[c].insert(y[c].end(), x[c].begin() + static_cast<long long>(pos), x[c].end());
    }
    return y;
}

// ---- spectrum ------------------------------------------------------------------------

std::vector<double> averageSpectrumDb(const Channels& x, double fs, int fftSize, double* binHz)
{
    const int n = fftSize, nb = n / 2 + 1;
    if (binHz)
        *binHz = fs / n;
    std::vector<double> power(static_cast<size_t>(nb), 0.0);
    Fft fft;
    fft.prepare(n);
    std::vector<double> win(static_cast<size_t>(n)), frame(static_cast<size_t>(n));
    double wsum = 0;
    for (int i = 0; i < n; ++i) {
        win[static_cast<size_t>(i)] = 0.5 - 0.5 * std::cos(2.0 * kPi * i / n);
        wsum += win[static_cast<size_t>(i)];
    }
    std::vector<cd> bins(static_cast<size_t>(nb)), scratch(static_cast<size_t>(n));
    const size_t len = frames(x);
    size_t count = 0;
    for (size_t start = 0; start == 0 || start + n <= len; start += static_cast<size_t>(n / 2)) {
        for (const auto& c : x) {
            for (int i = 0; i < n; ++i) {
                const size_t k = start + static_cast<size_t>(i);
                frame[static_cast<size_t>(i)] = k < len ? c[k] * win[static_cast<size_t>(i)] : 0.0;
            }
            fft.forwardReal(frame.data(), bins.data(), scratch.data());
            for (int k = 0; k < nb; ++k)
                power[static_cast<size_t>(k)] += std::norm(bins[static_cast<size_t>(k)]);
            ++count;
        }
        if (start + n >= len)
            break;
    }
    const double scale = 4.0 / (wsum * wsum) / std::max<size_t>(1, count);
    // 1/6-octave smoothing
    std::vector<double> pre(static_cast<size_t>(nb + 1), 0.0);
    for (int k = 0; k < nb; ++k)
        pre[static_cast<size_t>(k + 1)] = pre[static_cast<size_t>(k)] + power[static_cast<size_t>(k)] * scale;
    std::vector<double> db(static_cast<size_t>(nb));
    const double s = std::pow(2.0, 1.0 / 12.0);
    for (int k = 0; k < nb; ++k) {
        const int a = std::max(0, static_cast<int>(std::floor(k / s)));
        const int b = std::min(nb - 1, std::max(k, static_cast<int>(std::ceil(k * s))));
        const double mean = (pre[static_cast<size_t>(b + 1)] - pre[static_cast<size_t>(a)]) / (b - a + 1);
        db[static_cast<size_t>(k)] = 10.0 * std::log10(mean + 1e-24);
    }
    return db;
}

void convolveCentered(std::vector<float>& x, const std::vector<double>& fir)
{
    const size_t L = fir.size(), n = x.size();
    if (L == 0 || n == 0)
        return;
    const size_t centre = L / 2;
    const int M = nextPow2(std::max<size_t>(L * 2, 4096));
    const size_t B = static_cast<size_t>(M) - L + 1;
    Fft fft;
    fft.prepare(M);
    std::vector<cd> H(static_cast<size_t>(M / 2 + 1)), X(static_cast<size_t>(M / 2 + 1)), scratch(static_cast<size_t>(M));
    std::vector<double> buf(static_cast<size_t>(M), 0.0), out(n + L, 0.0);
    std::copy(fir.begin(), fir.end(), buf.begin());
    fft.forwardReal(buf.data(), H.data(), scratch.data());
    for (size_t pos = 0; pos < n; pos += B) {
        std::fill(buf.begin(), buf.end(), 0.0);
        const size_t m = std::min(B, n - pos);
        for (size_t i = 0; i < m; ++i)
            buf[i] = x[pos + i];
        fft.forwardReal(buf.data(), X.data(), scratch.data());
        for (size_t k = 0; k < X.size(); ++k)
            X[k] *= H[k];
        fft.inverseReal(X.data(), buf.data(), scratch.data());
        for (size_t i = 0; i < m + L - 1 && pos + i < out.size(); ++i)
            out[pos + i] += buf[i];
    }
    for (size_t i = 0; i < n; ++i)
        x[i] = static_cast<float>(out[i + centre]);
}

void eqMatch(Channels& x, double fs, const std::vector<double>& referenceDb, int fftSize, double amount, double maxDb)
{
    const int n = fftSize, nb = n / 2 + 1;
    if (static_cast<int>(referenceDb.size()) != nb || frames(x) == 0)
        return;
    const auto target = averageSpectrumDb(x, fs, n);
    std::vector<double> diff(static_cast<size_t>(nb));
    for (int k = 0; k < nb; ++k) {
        double d = (referenceDb[static_cast<size_t>(k)] - target[static_cast<size_t>(k)]) * amount;
        const double f = k * fs / n;
        if (f < 20.0 || f > fs * 0.45) // leave the extremes alone
            d = 0.0;
        diff[static_cast<size_t>(k)] = std::clamp(d, -maxDb, maxDb);
    }
    // linear-phase FIR from the (zero-phase) magnitude
    Fft fft;
    fft.prepare(n);
    std::vector<cd> H(static_cast<size_t>(nb)), scratch(static_cast<size_t>(n));
    for (int k = 0; k < nb; ++k)
        H[static_cast<size_t>(k)] = dbToGain(diff[static_cast<size_t>(k)]);
    std::vector<double> h(static_cast<size_t>(n));
    fft.inverseReal(H.data(), h.data(), scratch.data());
    std::vector<double> fir(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        const double w = 0.5 - 0.5 * std::cos(2.0 * kPi * i / n);
        fir[static_cast<size_t>(i)] = h[static_cast<size_t>((i + n / 2) % n)] * w;
    }
    for (auto& c : x)
        convolveCentered(c, fir);
}

// ---- statistics ------------------------------------------------------------------------

std::vector<ChannelStats> channelStats(const Channels& x, double fs)
{
    std::vector<ChannelStats> out;
    for (const auto& c : x) {
        ChannelStats s;
        double peak = 0, sq = 0, sum = 0;
        size_t run = 0;
        for (float v : c) {
            const double a = std::abs(v);
            peak = std::max(peak, a);
            sq += static_cast<double>(v) * v;
            sum += v;
            if (a >= 0.999) {
                if (++run == 3)
                    ++s.clippedRuns;
            } else {
                run = 0;
            }
        }
        const double n = std::max<double>(1.0, static_cast<double>(c.size()));
        const double rms = std::sqrt(sq / n);
        s.peakDb = gainToDb(peak);
        s.rmsDb = gainToDb(rms);
        s.dcPercent = 100.0 * sum / n;
        s.crestDb = rms > 0 ? gainToDb(peak / rms) : 0.0;
        s.truePeakDb = truePeakDb(Channels { c }, fs);
        out.push_back(s);
    }
    return out;
}

// ---- export ---------------------------------------------------------------------------

void ditherTo(Channels& x, int bits, bool noiseShaped, unsigned seed)
{
    if (bits >= 32 || bits < 2)
        return;
    const double S = std::ldexp(1.0, bits - 1);
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> u(0.0, 1.0);
    // F-weighted-ish error feedback (3 taps): pushes noise towards the top octave
    const double h[3] = { 1.623, -0.982, 0.109 };
    for (auto& c : x) {
        double e1 = 0, e2 = 0, e3 = 0;
        for (auto& v : c) {
            double s = v * S;
            if (noiseShaped)
                s -= h[0] * e1 + h[1] * e2 + h[2] * e3;
            const double d = u(rng) - u(rng); // TPDF, +/- 1 LSB
            double q = std::round(s + d);
            q = std::clamp(q, -S, S - 1.0);
            const double e = q - s;
            e3 = e2;
            e2 = e1;
            e1 = std::clamp(e, -4.0, 4.0); // keep the feedback stable at full scale
            v = static_cast<float>(q / S);
        }
    }
}

// ---- spectral ---------------------------------------------------------------------------

void spectralBlend(Channels& processed, const Channels& original, double fs, double loHz, double hiHz)
{
    if (frames(processed) != frames(original) || processed.size() != original.size())
        return;
    const int n = 2048 * std::max(1, static_cast<int>(std::lround(fs / 48000.0)));
    const double binHz = fs / n;
    const double kLo = loHz / binHz, kHi = hiHz / binHz;
    std::vector<double> w(static_cast<size_t>(n / 2 + 1));
    for (size_t k = 0; k < w.size(); ++k)
        w[k] = bandWeight(static_cast<double>(k), kLo, kHi);
    // difference = processed - original, band-limited, added back to original
    Channels diff = processed;
    for (size_t c = 0; c < diff.size(); ++c)
        for (size_t i = 0; i < diff[c].size(); ++i)
            diff[c][i] -= original[c][i];
    const Channels band = stftProcess(diff, n, [&](long long, long long, std::vector<std::vector<cd>>& bins) {
        for (auto& b : bins)
            for (size_t k = 0; k < b.size(); ++k)
                b[k] *= w[k];
    });
    for (size_t c = 0; c < processed.size(); ++c)
        for (size_t i = 0; i < processed[c].size(); ++i)
            processed[c][i] = original[c][i] + band[c][i];
}

void spectralRepair(Channels& x, double fs, size_t start, size_t length, double loHz, double hiHz, SpectralMode mode,
                    double strength, double gainDb)
{
    const size_t len = frames(x);
    if (length == 0 || start + length > len)
        return;
    const int n = 2048 * std::max(1, static_cast<int>(std::lround(fs / 48000.0)));
    const int nb = n / 2 + 1;
    const double binHz = fs / n;
    const double kLo = std::max(0.0, loHz / binHz), kHi = std::min(static_cast<double>(nb - 1), hiHz / binHz);
    std::vector<double> wk(static_cast<size_t>(nb));
    for (int k = 0; k < nb; ++k)
        wk[static_cast<size_t>(k)] = bandWeight(k, kLo, kHi);
    strength = std::clamp(strength, 0.0, 1.0);
    const long long s0 = static_cast<long long>(start), s1 = static_cast<long long>(start + length);
    const long long half = n / 2;

    // reference spectra from frames wholly before and after the box
    std::vector<double> refL(static_cast<size_t>(nb), 0.0), refR(static_cast<size_t>(nb), 0.0);
    int cntL = 0, cntR = 0;
    const long long ctx = static_cast<long long>(std::max(0.25 * fs, static_cast<double>(n)));
    if (mode != SpectralMode::Gain) {
        stftProcess(x, n, [&](long long, long long centre, std::vector<std::vector<cd>>& bins) {
            const bool left = centre + half <= s0 && centre >= s0 - ctx;
            const bool right = centre - half >= s1 && centre <= s1 + ctx;
            if (!left && !right)
                return;
            auto& ref = left ? refL : refR;
            (left ? cntL : cntR)++;
            for (auto& b : bins)
                for (int k = 0; k < nb; ++k)
                    ref[static_cast<size_t>(k)] += std::norm(b[static_cast<size_t>(k)]) / static_cast<double>(bins.size());
        });
        if (cntL == 0 && cntR == 0)
            return; // nothing to learn from
        for (int k = 0; k < nb; ++k) {
            if (cntL)
                refL[static_cast<size_t>(k)] = std::sqrt(refL[static_cast<size_t>(k)] / cntL);
            if (cntR)
                refR[static_cast<size_t>(k)] = std::sqrt(refR[static_cast<size_t>(k)] / cntR);
        }
        if (!cntL)
            refL = refR;
        if (!cntR)
            refR = refL;
    }
    const double gainLin = dbToGain(gainDb);
    x = stftProcess(x, n, [&](long long, long long centre, std::vector<std::vector<cd>>& bins) {
        // time weight: full inside the box, fading over half a frame at its edges
        const double dIn = std::min(static_cast<double>(centre - s0), static_cast<double>(s1 - centre));
        const double wt = smoothstep(dIn / half + 0.5);
        if (wt <= 0.0)
            return;
        const double tau = std::clamp(static_cast<double>(centre - s0) / static_cast<double>(length), 0.0, 1.0);
        for (auto& b : bins)
            for (int k = 0; k < nb; ++k) {
                const double w = wt * wk[static_cast<size_t>(k)];
                if (w <= 0.0)
                    continue;
                double g = 1.0;
                if (mode == SpectralMode::Gain) {
                    g = gainLin;
                } else {
                    const double l = refL[static_cast<size_t>(k)], r = refR[static_cast<size_t>(k)];
                    const double ref = std::exp((1.0 - tau) * std::log(l + 1e-12) + tau * std::log(r + 1e-12));
                    const double mag = std::abs(b[static_cast<size_t>(k)]) + 1e-12;
                    if (mode == SpectralMode::Attenuate)
                        g = std::pow(std::min(1.0, ref / mag), strength);
                    else
                        g = 1.0 + strength * (std::min(10.0, ref / mag) - 1.0);
                }
                b[static_cast<size_t>(k)] *= 1.0 + w * (g - 1.0);
            }
    });
}

} // namespace mend::offline
