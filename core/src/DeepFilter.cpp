// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
//
// DeepFilterNet3 inference, written from the published architecture and
// checked against the reference implementation (libDF / tract). The
// weights are DeepFilterNet3's released ones (MIT or Apache-2.0).
#include "mend/DeepFilter.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace mend {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int C = DeepFilterNet::kChannels;
constexpr int H = DeepFilterNet::kHidden;
constexpr int E = DeepFilterNet::kEmb;
constexpr int NERB = DeepFilterNet::kErb;
constexpr int NDF = DeepFilterNet::kDf;
constexpr int ORDER = DeepFilterNet::kOrder;

inline float sigmoid(float x) noexcept { return 1.0f / (1.0f + std::exp(-x)); }
inline float relu(float x) noexcept { return x > 0.0f ? x : 0.0f; }

// out[g*Hg+h] = sum_i in[g*Ig+i] * W[(g*Ig+i)*Hg+h]   (DeepFilterNet's GroupedLinearEinsum)
void groupedLinear(const float* in, const float* W, int groups, int Ig, int Hg, float* out) noexcept
{
    for (int g = 0; g < groups; ++g) {
        float* o = out + g * Hg;
        std::fill(o, o + Hg, 0.0f);
        const float* x = in + g * Ig;
        const float* w = W + static_cast<std::size_t>(g) * Ig * Hg;
        for (int i = 0; i < Ig; ++i) {
            const float xi = x[i];
            const float* wr = w + static_cast<std::size_t>(i) * Hg;
            for (int h = 0; h < Hg; ++h)
                o[h] += xi * wr[h];
        }
    }
}

// ONNX GRU, gate order z, r, h, linear_before_reset = 1. W [3H, In], R [3H, H], B [6H].
void gruStep(const float* x, int In, float* h, const float* W, const float* R, const float* B, float* scratch) noexcept
{
    float* gx = scratch;         // 3H
    float* gh = scratch + 3 * H; // 3H
    for (int r = 0; r < 3 * H; ++r) {
        const float* wr = W + static_cast<std::size_t>(r) * In;
        float a = B[r];
        for (int i = 0; i < In; ++i)
            a += wr[i] * x[i];
        gx[r] = a;
        const float* rr = R + static_cast<std::size_t>(r) * H;
        float b = B[3 * H + r];
        for (int i = 0; i < H; ++i)
            b += rr[i] * h[i];
        gh[r] = b;
    }
    for (int i = 0; i < H; ++i) {
        const float z = sigmoid(gx[i] + gh[i]);
        const float rg = sigmoid(gx[H + i] + gh[H + i]);
        const float n = std::tanh(gx[2 * H + i] + rg * gh[2 * H + i]);
        h[i] = (1.0f - z) * n + z * h[i];
    }
}

// 1x1 convolution over channels: out[o][f] = b[o] + sum_c W[o][c] in[c][f]
void conv1x1(const float* in, int Cin, int F, const float* W, const float* b, int Cout, float* out, bool doRelu) noexcept
{
    for (int o = 0; o < Cout; ++o) {
        float* y = out + o * F;
        const float bias = b ? b[o] : 0.0f;
        std::fill(y, y + F, bias);
        for (int c = 0; c < Cin; ++c) {
            const float w = W[o * Cin + c];
            const float* x = in + c * F;
            for (int f = 0; f < F; ++f)
                y[f] += w * x[f];
        }
        if (doRelu)
            for (int f = 0; f < F; ++f)
                y[f] = relu(y[f]);
    }
}

// Depthwise 1x3 convolution along frequency, padding 1, stride 1 or 2.
void dwConvFreq(const float* in, int Ch, int F, const float* w, int stride, float* out) noexcept
{
    const int Fo = (F + 2 - 3) / stride + 1;
    for (int c = 0; c < Ch; ++c) {
        const float* x = in + c * F;
        const float* k = w + c * 3;
        float* y = out + c * Fo;
        for (int j = 0; j < Fo; ++j) {
            float a = 0.0f;
            for (int t = 0; t < 3; ++t) {
                const int i = stride * j + t - 1;
                if (i >= 0 && i < F)
                    a += k[t] * x[i];
            }
            y[j] = a;
        }
    }
}

// Depthwise transposed 1x3 convolution along frequency, stride 2, padding 1,
// output padding 1: F -> 2F.
void dwConvTFreq(const float* in, int Ch, int F, const float* w, float* out) noexcept
{
    const int Fo = 2 * F;
    for (int c = 0; c < Ch; ++c) {
        const float* x = in + c * F;
        const float* k = w + c * 3;
        float* y = out + c * Fo;
        std::fill(y, y + Fo, 0.0f);
        for (int i = 0; i < F; ++i)
            for (int t = 0; t < 3; ++t) {
                const int j = 2 * i + t - 1;
                if (j >= 0 && j < Fo)
                    y[j] += x[i] * k[t];
            }
    }
}

} // namespace

// ---------------------------------------------------------------------------

struct DeepFilterNet::Chan {
    std::array<float, kFft - kHop> anaMem {}, synMem {};
    // noisy history (oldest first, kOrder frames) and the frames waiting for
    // their gains (kOrder + look-ahead)
    std::vector<std::vector<std::complex<float>>> specX, specY;
    Chan()
        : specX(kOrder, std::vector<std::complex<float>>(kBins))
        , specY(kOrder + kLookahead, std::vector<std::complex<float>>(kBins))
    {
    }
    void reset()
    {
        anaMem.fill(0.0f);
        synMem.fill(0.0f);
        for (auto& v : specX)
            std::fill(v.begin(), v.end(), std::complex<float> {});
        for (auto& v : specY)
            std::fill(v.begin(), v.end(), std::complex<float> {});
    }
};

struct DeepFilterNet::Weights {
    const float *encConv0w, *encConv0b, *encConv1w, *encConv2w, *encConv2b, *encConv3w, *encConv4w, *encConv4b,
        *encConv5w, *encConv6w, *encConv6b, *encConv7w, *encConv8w, *encConv8b, *encConv9w, *encConv10w, *encConv10b,
        *encLin0, *encLin1, *encGruW, *encGruR, *encGruB, *encLin2, *encMm, *encMmB;
    const float *erbLin0, *erbGru0W, *erbGru0R, *erbGru0B, *erbGru1W, *erbGru1R, *erbGru1B, *erbLin1, *erbConv0w,
        *erbConv0b, *erbConv1w, *erbConv2w, *erbConv2b, *erbConv3w, *erbConv3b, *erbConvT4w, *erbConv5w, *erbConv5b,
        *erbConv6w, *erbConv6b, *erbConvT7w, *erbConv8w, *erbConv8b, *erbConv9w, *erbConv9b, *erbConv10w, *erbConv10b;
    const float *dfLin0, *dfGru0W, *dfGru0R, *dfGru0B, *dfGru1W, *dfGru1R, *dfGru1B, *dfLin1, *dfConv0w, *dfConv1w,
        *dfConv1b, *dfLin2;

    // Assigns the pointers in blob order; returns the number of floats used.
    std::size_t bind(const float* p)
    {
        const float* const start = p;
        auto take = [&p](std::size_t n) {
            const float* r = p;
            p += n;
            return r;
        };
        const std::size_t G3 = 3u * H;
        encConv0w = take(64 * 9);
        encConv0b = take(64);
        encConv1w = take(64 * 3);
        encConv2w = take(64 * 64);
        encConv2b = take(64);
        encConv3w = take(64 * 3);
        encConv4w = take(64 * 64);
        encConv4b = take(64);
        encConv5w = take(64 * 3);
        encConv6w = take(64 * 64);
        encConv6b = take(64);
        encConv7w = take(64 * 9);
        encConv8w = take(64 * 64);
        encConv8b = take(64);
        encConv9w = take(64 * 3);
        encConv10w = take(64 * 64);
        encConv10b = take(64);
        encLin0 = take(32 * 96 * 16);
        encLin1 = take(16 * 32 * 16);
        encGruW = take(G3 * H);
        encGruR = take(G3 * H);
        encGruB = take(2 * G3);
        encLin2 = take(16 * 16 * 32);
        encMm = take(512);
        encMmB = take(1);

        erbLin0 = take(16 * 32 * 16);
        erbGru0W = take(G3 * H);
        erbGru0R = take(G3 * H);
        erbGru0B = take(2 * G3);
        erbGru1W = take(G3 * H);
        erbGru1R = take(G3 * H);
        erbGru1B = take(2 * G3);
        erbLin1 = take(16 * 16 * 32);
        erbConv0w = take(64);
        erbConv0b = take(64);
        erbConv1w = take(64 * 3);
        erbConv2w = take(64 * 64);
        erbConv2b = take(64);
        erbConv3w = take(64);
        erbConv3b = take(64);
        erbConvT4w = take(64 * 3);
        erbConv5w = take(64 * 64);
        erbConv5b = take(64);
        erbConv6w = take(64);
        erbConv6b = take(64);
        erbConvT7w = take(64 * 3);
        erbConv8w = take(64 * 64);
        erbConv8b = take(64);
        erbConv9w = take(64);
        erbConv9b = take(64);
        erbConv10w = take(64 * 3);
        erbConv10b = take(1);

        dfLin0 = take(8 * 64 * 32);
        dfGru0W = take(G3 * H);
        dfGru0R = take(G3 * H);
        dfGru0B = take(2 * G3);
        dfGru1W = take(G3 * H);
        dfGru1R = take(G3 * H);
        dfGru1B = take(2 * G3);
        dfLin1 = take(16 * 32 * 16);
        dfConv0w = take(10 * 32 * 5);
        dfConv1w = take(10 * 10);
        dfConv1b = take(10);
        dfLin2 = take(16 * 16 * 60);
        return static_cast<std::size_t>(p - start);
    }
};

// Mixed-radix complex FFT for N = 960 (factors 4·4·4·3·5).
struct DeepFilterNet::Fft {
    static constexpr int N = kFft;
    std::array<std::complex<float>, N> tw;
    std::array<int, 5> factors { 4, 4, 4, 3, 5 };
    std::array<std::complex<float>, N> buf;

    Fft()
    {
        for (int i = 0; i < N; ++i)
            tw[i] = std::polar(1.0f, static_cast<float>(-2.0 * kPi * i / N));
    }

    void rec(const std::complex<float>* in, std::complex<float>* out, int n, int stride, int level) noexcept
    {
        if (n == 1) {
            out[0] = in[0];
            return;
        }
        const int p = factors[static_cast<size_t>(level)];
        const int m = n / p;
        for (int q = 0; q < p; ++q)
            rec(in + q * stride, out + q * m, m, stride * p, level + 1);
        const int twStep = N / n; // W_n^x = W_N^(x * N/n)
        std::complex<float> t[5];
        for (int k = 0; k < m; ++k) {
            for (int q = 0; q < p; ++q)
                t[q] = out[q * m + k] * tw[static_cast<size_t>((q * k * twStep) % N)];
            for (int s = 0; s < p; ++s) {
                std::complex<float> a = 0.0f;
                for (int q = 0; q < p; ++q)
                    a += t[q] * tw[static_cast<size_t>(((q * s) % p) * (N / p))];
                out[k + m * s] = a;
            }
        }
    }

    // out = DFT(in), unnormalised
    void forward(const std::complex<float>* in, std::complex<float>* out) noexcept { rec(in, out, N, 1, 0); }
};

// ---------------------------------------------------------------------------

std::size_t DeepFilterNet::weightCount()
{
    return 2134384; // checked against Weights::bind() in setWeights()
}

DeepFilterNet::DeepFilterNet()
    : W_(new Weights {})
    , fft_(new Fft)
{
    // Vorbis window, as libDF
    for (int i = 0; i < kFft; ++i) {
        const double s = std::sin(0.5 * kPi * (i + 0.5) / (kFft / 2));
        window_[static_cast<size_t>(i)] = static_cast<float>(std::sin(0.5 * kPi * s * s));
    }
    // ERB band widths (libDF erb_fb: 32 bands, at least 2 bins each)
    auto freq2erb = [](double f) { return 9.265 * std::log1p(f / (24.7 * 9.265)); };
    auto erb2freq = [](double e) { return 24.7 * 9.265 * (std::exp(e / 9.265) - 1.0); };
    const double fw = static_cast<double>(kSampleRate) / kFft;
    const double lo = freq2erb(0.0), hi = freq2erb(kSampleRate / 2.0);
    const double step = (hi - lo) / NERB;
    int prev = 0, over = 0;
    for (int i = 1; i <= NERB; ++i) {
        const double f = erb2freq(lo + i * step);
        const int fb = static_cast<int>(std::round(static_cast<float>(f / fw)));
        int nb = fb - prev - over;
        if (nb < 2) {
            over = 2 - nb;
            nb = 2;
        } else {
            over = 0;
        }
        erbWidth_[static_cast<size_t>(i - 1)] = nb;
        prev = fb;
    }
    erbWidth_[NERB - 1] += 1;
    int sum = 0;
    for (int w : erbWidth_)
        sum += w;
    if (sum > kBins)
        erbWidth_[NERB - 1] -= sum - kBins;

    alpha_ = 0.99f; // exp(-hop / (sr * tau)), tau = 1 s, rounded as libDF does

    mix_.assign(kBins, cf {});
    e0_.assign(C * NERB, 0.0f);
    e1_.assign(C * NERB / 2, 0.0f);
    e2_.assign(C * NERB / 4, 0.0f);
    e3_.assign(C * NERB / 4, 0.0f);
    c0_.assign(C * NDF, 0.0f);
    emb_.assign(E, 0.0f);
    c0Hist_.assign(ORDER, std::vector<float>(C * NDF, 0.0f));
    hEnc_.assign(H, 0.0f);
    hErb0_.assign(H, 0.0f);
    hErb1_.assign(H, 0.0f);
    hDf0_.assign(H, 0.0f);
    hDf1_.assign(H, 0.0f);
    coefs_.assign(NDF * ORDER * 2, 0.0f);
    s0_.assign(C * NDF, 0.0f);
    s1_.assign(C * NDF, 0.0f);
    s2_.assign(6 * H + 2 * E, 0.0f);
    s3_.assign(16 * 60, 0.0f);
    frame_.assign(kFft, cf {});
    out_.assign(kFft, cf {});
    time_.assign(kFft, 0.0f);
    setChannels(1);
}

DeepFilterNet::~DeepFilterNet()
{
    for (Chan* c : ch_)
        delete c;
    delete W_;
    delete fft_;
}

void DeepFilterNet::setChannels(int n)
{
    n = std::clamp(n, 1, kMaxChannels);
    while (static_cast<int>(ch_.size()) < n)
        ch_.push_back(new Chan);
    nch_ = n;
    reset();
}

bool DeepFilterNet::setWeights(const float* weights, std::size_t count)
{
    if (weights == nullptr || count != weightCount())
        return false;
    if (W_->bind(weights) != count)
        return false;
    w_ = weights;
    return true;
}

void DeepFilterNet::setAttenuationLimitDb(float db) noexcept
{
    db = std::fabs(db);
    attenLim_ = db >= 100.0f ? 0.0f : std::pow(10.0f, -db / 20.0f);
}

void DeepFilterNet::reset()
{
    for (Chan* c : ch_)
        c->reset();
    for (int i = 0; i < NERB; ++i)
        meanNorm_[static_cast<size_t>(i)] = -60.0f + i * (-90.0f + 60.0f) / (NERB - 1);
    for (int i = 0; i < NDF; ++i)
        unitNorm_[static_cast<size_t>(i)] = 0.001f + i * (0.0001f - 0.001f) / (NDF - 1);
    for (auto& h : featErbHist_)
        h.fill(0.0f);
    for (auto& h : featSpecHist_)
        h.fill(0.0f);
    for (auto& h : c0Hist_)
        std::fill(h.begin(), h.end(), 0.0f);
    for (auto* v : { &hEnc_, &hErb0_, &hErb1_, &hDf0_, &hDf1_ })
        std::fill(v->begin(), v->end(), 0.0f);
    mask_.fill(1.0f);
    lsnr_ = 0.0f;
    skipCounter_ = 0;
}

void DeepFilterNet::analysis(Chan& ch, const float* in, cf* spec) noexcept
{
    constexpr int M = kFft - kHop;
    for (int i = 0; i < M; ++i)
        frame_[static_cast<size_t>(i)] = cf(ch.anaMem[static_cast<size_t>(i)] * window_[static_cast<size_t>(i)], 0.0f);
    for (int i = 0; i < kHop; ++i)
        frame_[static_cast<size_t>(M + i)] = cf(in[i] * window_[static_cast<size_t>(M + i)], 0.0f);
    // shift memory (hop = half the window here, so it is just the new input)
    if (M > kHop)
        std::memmove(ch.anaMem.data(), ch.anaMem.data() + kHop, sizeof(float) * (M - kHop));
    std::memcpy(ch.anaMem.data() + (M - kHop), in, sizeof(float) * kHop);
    fft_->forward(frame_.data(), out_.data());
    const float norm = 1.0f / (static_cast<float>(kFft) * kFft / (2.0f * kHop));
    for (int k = 0; k < kBins; ++k)
        spec[k] = out_[static_cast<size_t>(k)] * norm;
}

void DeepFilterNet::synthesis(Chan& ch, cf* spec, float* out) noexcept
{
    // inverse real FFT (unnormalised) via a forward FFT of the conjugate
    frame_[0] = std::conj(cf(spec[0].real(), 0.0f));
    for (int k = 1; k < kBins - 1; ++k) {
        frame_[static_cast<size_t>(k)] = std::conj(spec[k]);
        frame_[static_cast<size_t>(kFft - k)] = spec[k];
    }
    frame_[kBins - 1] = cf(spec[kBins - 1].real(), 0.0f);
    fft_->forward(frame_.data(), out_.data());
    for (int i = 0; i < kFft; ++i)
        time_[static_cast<size_t>(i)] = out_[static_cast<size_t>(i)].real() * window_[static_cast<size_t>(i)];
    constexpr int M = kFft - kHop;
    for (int i = 0; i < kHop; ++i)
        out[i] = time_[static_cast<size_t>(i)] + ch.synMem[static_cast<size_t>(i)];
    // next overlap: the tail of this frame
    for (int i = 0; i < M; ++i)
        ch.synMem[static_cast<size_t>(i)] = time_[static_cast<size_t>(kHop + i)];
}

void DeepFilterNet::encoder() noexcept
{
    const Weights& w = *W_;
    // erb_conv0: 3x3 over (time, freq), 1 -> 64 channels, BN folded, ReLU
    for (int c = 0; c < C; ++c) {
        const float* k = w.encConv0w + c * 9;
        for (int f = 0; f < NERB; ++f) {
            float a = w.encConv0b[c];
            for (int dt = 0; dt < 3; ++dt)
                for (int df = 0; df < 3; ++df) {
                    const int ff = f + df - 1;
                    if (ff >= 0 && ff < NERB)
                        a += k[dt * 3 + df] * featErbHist_[static_cast<size_t>(dt)][static_cast<size_t>(ff)];
                }
            e0_[static_cast<size_t>(c * NERB + f)] = relu(a);
        }
    }
    dwConvFreq(e0_.data(), C, NERB, w.encConv1w, 2, s0_.data());
    conv1x1(s0_.data(), C, NERB / 2, w.encConv2w, w.encConv2b, C, e1_.data(), true);
    dwConvFreq(e1_.data(), C, NERB / 2, w.encConv3w, 2, s0_.data());
    conv1x1(s0_.data(), C, NERB / 4, w.encConv4w, w.encConv4b, C, e2_.data(), true);
    dwConvFreq(e2_.data(), C, NERB / 4, w.encConv5w, 1, s0_.data());
    conv1x1(s0_.data(), C, NERB / 4, w.encConv6w, w.encConv6b, C, e3_.data(), true);

    // df_conv0: 3x3, 2 input channels (re, im) in 2 groups -> 64, then 1x1, ReLU
    for (int c = 0; c < C; ++c) {
        const float* k = w.encConv7w + c * 9;
        const int ch = c < C / 2 ? 0 : 1;
        for (int f = 0; f < NDF; ++f) {
            float a = 0.0f;
            for (int dt = 0; dt < 3; ++dt)
                for (int df = 0; df < 3; ++df) {
                    const int ff = f + df - 1;
                    if (ff >= 0 && ff < NDF)
                        a += k[dt * 3 + df] * featSpecHist_[static_cast<size_t>(dt)][static_cast<size_t>(ch * NDF + ff)];
                }
            s0_[static_cast<size_t>(c * NDF + f)] = a;
        }
    }
    conv1x1(s0_.data(), C, NDF, w.encConv8w, w.encConv8b, C, c0_.data(), true);
    dwConvFreq(c0_.data(), C, NDF, w.encConv9w, 2, s0_.data());
    conv1x1(s0_.data(), C, NDF / 2, w.encConv10w, w.encConv10b, C, s1_.data(), true); // c1 [64][48]

    // flatten c1 as [freq][channel] and project: 3072 -> 512, ReLU
    float* cflat = s0_.data();
    for (int f = 0; f < NDF / 2; ++f)
        for (int c = 0; c < C; ++c)
            cflat[f * C + c] = s1_[static_cast<size_t>(c * (NDF / 2) + f)];
    float* cemb = s2_.data() + 6 * H; // E floats
    groupedLinear(cflat, w.encLin0, 32, 96, 16, cemb);
    for (int i = 0; i < E; ++i)
        cemb[i] = relu(cemb[i]);
    // emb = flatten(e3 as [freq][channel]) + cemb
    float* embIn = s2_.data() + 6 * H + E; // E floats
    for (int f = 0; f < NERB / 4; ++f)
        for (int c = 0; c < C; ++c)
            embIn[f * C + c] = e3_[static_cast<size_t>(c * (NERB / 4) + f)] + cemb[f * C + c];
    // SqueezedGRU: 512 -> 256 (ReLU), GRU, 256 -> 512 (ReLU)
    float* x = s1_.data(); // H floats
    groupedLinear(embIn, w.encLin1, 16, 32, 16, x);
    for (int i = 0; i < H; ++i)
        x[i] = relu(x[i]);
    gruStep(x, H, hEnc_.data(), w.encGruW, w.encGruR, w.encGruB, s2_.data());
    groupedLinear(hEnc_.data(), w.encLin2, 16, 16, 32, emb_.data());
    for (int i = 0; i < E; ++i)
        emb_[static_cast<size_t>(i)] = relu(emb_[static_cast<size_t>(i)]);
    float l = w.encMmB[0];
    for (int i = 0; i < E; ++i)
        l += emb_[static_cast<size_t>(i)] * w.encMm[i];
    lsnr_ = sigmoid(l) * 50.0f - 15.0f;
}

void DeepFilterNet::erbDecoder() noexcept
{
    const Weights& w = *W_;
    float* x = s1_.data(); // H
    groupedLinear(emb_.data(), w.erbLin0, 16, 32, 16, x);
    for (int i = 0; i < H; ++i)
        x[i] = relu(x[i]);
    gruStep(x, H, hErb0_.data(), w.erbGru0W, w.erbGru0R, w.erbGru0B, s2_.data());
    gruStep(hErb0_.data(), H, hErb1_.data(), w.erbGru1W, w.erbGru1R, w.erbGru1B, s2_.data());
    float* y = s2_.data() + 6 * H; // E
    groupedLinear(hErb1_.data(), w.erbLin1, 16, 16, 32, y);
    // view as [freq 8][channel 64] -> [channel][freq], ReLU
    float* d = s0_.data(); // 64 x 32 max
    float* t = s1_.data() + H;
    const int F3 = NERB / 4;
    // conv3p(e3) + emb
    for (int c = 0; c < C; ++c)
        for (int f = 0; f < F3; ++f)
            t[c * F3 + f] = relu(w.erbConv0w[c] * e3_[static_cast<size_t>(c * F3 + f)] + w.erbConv0b[c])
                + relu(y[f * C + c]);
    // convt3: depthwise 1x3 + 1x1, ReLU
    dwConvFreq(t, C, F3, w.erbConv1w, 1, d);
    conv1x1(d, C, F3, w.erbConv2w, w.erbConv2b, C, t + C * F3, true); // d3 at t + C*F3
    const float* d3 = t + C * F3;
    // conv2p(e2) + d3 -> convt2 (transposed, 8 -> 16) + 1x1, ReLU
    for (int c = 0; c < C; ++c)
        for (int f = 0; f < F3; ++f)
            d[c * F3 + f] = relu(w.erbConv3w[c] * e2_[static_cast<size_t>(c * F3 + f)] + w.erbConv3b[c]) + d3[c * F3 + f];
    dwConvTFreq(d, C, F3, w.erbConvT4w, t);
    const int F2 = NERB / 2;
    conv1x1(t, C, F2, w.erbConv5w, w.erbConv5b, C, d, true); // d2 in d
    // conv1p(e1) + d2 -> convt1 (16 -> 32) + 1x1, ReLU
    for (int c = 0; c < C; ++c)
        for (int f = 0; f < F2; ++f)
            t[c * F2 + f] = relu(w.erbConv6w[c] * e1_[static_cast<size_t>(c * F2 + f)] + w.erbConv6b[c]) + d[c * F2 + f];
    dwConvTFreq(t, C, F2, w.erbConvT7w, d);
    conv1x1(d, C, NERB, w.erbConv8w, w.erbConv8b, C, t, true); // d1 in t
    // conv0p(e0) + d1 -> conv0_out: 1x3, 64 -> 1, sigmoid
    for (int c = 0; c < C; ++c)
        for (int f = 0; f < NERB; ++f)
            d[c * NERB + f] = relu(w.erbConv9w[c] * e0_[static_cast<size_t>(c * NERB + f)] + w.erbConv9b[c]) + t[c * NERB + f];
    for (int f = 0; f < NERB; ++f) {
        float a = w.erbConv10b[0];
        for (int c = 0; c < C; ++c)
            for (int k = 0; k < 3; ++k) {
                const int ff = f + k - 1;
                if (ff >= 0 && ff < NERB)
                    a += w.erbConv10w[c * 3 + k] * d[c * NERB + ff];
            }
        mask_[static_cast<size_t>(f)] = sigmoid(a);
    }
}

void DeepFilterNet::dfDecoder() noexcept
{
    const Weights& w = *W_;
    float* x = s1_.data(); // H
    groupedLinear(emb_.data(), w.dfLin0, 8, 64, 32, x);
    for (int i = 0; i < H; ++i)
        x[i] = relu(x[i]);
    gruStep(x, H, hDf0_.data(), w.dfGru0W, w.dfGru0R, w.dfGru0B, s2_.data());
    gruStep(hDf0_.data(), H, hDf1_.data(), w.dfGru1W, w.dfGru1R, w.dfGru1B, s2_.data());
    float* c = s1_.data() + H; // H
    groupedLinear(emb_.data(), w.dfLin1, 16, 32, 16, c);
    for (int i = 0; i < H; ++i)
        c[i] += hDf1_[static_cast<size_t>(i)];
    float* q = s3_.data(); // 960
    groupedLinear(c, w.dfLin2, 16, 16, 60, q);
    // df_convp over the last ORDER frames of c0: (5 x 1) kernel, 64 -> 10 in 2 groups, then 1x1, ReLU
    float* p0 = s0_.data(); // 10 x 96
    for (int o = 0; o < 10; ++o) {
        const int base = o < 5 ? 0 : 32;
        for (int f = 0; f < NDF; ++f) {
            float a = 0.0f;
            for (int i = 0; i < 32; ++i) {
                const float* k = w.dfConv0w + (o * 32 + i) * 5;
                for (int dt = 0; dt < ORDER; ++dt)
                    a += k[dt] * c0Hist_[static_cast<size_t>(dt)][static_cast<size_t>((base + i) * NDF + f)];
            }
            p0[o * NDF + f] = a;
        }
    }
    float* p = s0_.data() + 10 * NDF;
    conv1x1(p0, 10, NDF, w.dfConv1w, w.dfConv1b, 10, p, true);
    for (int f = 0; f < NDF; ++f)
        for (int j = 0; j < 10; ++j)
            coefs_[static_cast<size_t>(f * 10 + j)] = std::tanh(q[f * 10 + j]) + p[j * NDF + f];
}

float DeepFilterNet::processHop(const float* const* in, float* const* out) noexcept
{
    if (w_ == nullptr) {
        for (int c = 0; c < nch_; ++c)
            std::memcpy(out[c], in[c], sizeof(float) * kHop);
        return 0.0f;
    }
    // digital silence: after a few frames, output silence without running the model
    float e = 0.0f;
    for (int c = 0; c < nch_; ++c)
        for (int i = 0; i < kHop; ++i)
            e += in[c][i] * in[c][i];
    skipCounter_ = e / (kHop * nch_) < 1e-7f ? skipCounter_ + 1 : 0;
    if (skipCounter_ > 5) {
        for (int c = 0; c < nch_; ++c)
            std::fill(out[c], out[c] + kHop, 0.0f);
        return -15.0f;
    }

    // rotate the spectrum histories and analyse the new hop into the newest slot
    std::fill(mix_.begin(), mix_.end(), cf {});
    const float inv = 1.0f / static_cast<float>(nch_);
    for (int c = 0; c < nch_; ++c) {
        Chan& ch = *ch_[static_cast<size_t>(c)];
        std::rotate(ch.specY.begin(), ch.specY.begin() + 1, ch.specY.end());
        std::rotate(ch.specX.begin(), ch.specX.begin() + 1, ch.specX.end());
        cf* spec = ch.specY.back().data();
        analysis(ch, in[c], spec);
        ch.specX.back() = ch.specY.back();
        for (int k = 0; k < kBins; ++k)
            mix_[static_cast<size_t>(k)] += spec[k] * inv;
    }

    if (attenLim_ >= 1.0f) {
        for (int c = 0; c < nch_; ++c)
            std::memcpy(out[c], in[c], sizeof(float) * kHop);
        return 35.0f;
    }

    // features of the mix
    const cf* spec = mix_.data();
    std::rotate(featErbHist_.begin(), featErbHist_.begin() + 1, featErbHist_.end());
    std::rotate(featSpecHist_.begin(), featSpecHist_.begin() + 1, featSpecHist_.end());
    {
        auto& fe = featErbHist_.back();
        int k = 0;
        for (int b = 0; b < NERB; ++b) {
            const int n = erbWidth_[static_cast<size_t>(b)];
            float s = 0.0f;
            for (int j = 0; j < n; ++j, ++k)
                s += std::norm(spec[k]);
            float v = 10.0f * std::log10(s / n + 1e-10f);
            float& m = meanNorm_[static_cast<size_t>(b)];
            m = v * (1.0f - alpha_) + m * alpha_;
            fe[static_cast<size_t>(b)] = (v - m) / 40.0f;
        }
        auto& fs = featSpecHist_.back();
        for (int f = 0; f < NDF; ++f) {
            float& u = unitNorm_[static_cast<size_t>(f)];
            u = std::abs(spec[f]) * (1.0f - alpha_) + u * alpha_;
            const float r = 1.0f / std::sqrt(u);
            fs[static_cast<size_t>(f)] = spec[f].real() * r;
            fs[static_cast<size_t>(NDF + f)] = spec[f].imag() * r;
        }
    }

    encoder();
    std::rotate(c0Hist_.begin(), c0Hist_.begin() + 1, c0Hist_.end());
    c0Hist_.back() = c0_;

    const bool zeros = lsnr_ < minDbThresh;
    const bool gains = !zeros && lsnr_ <= maxDbErbThresh;
    const bool df = gains && lsnr_ <= maxDbDfThresh;
    if (gains) {
        erbDecoder();
        skipCounter_ = 0;
    } else if (zeros) {
        mask_.fill(0.0f);
        skipCounter_ = 0;
    } else {
        ++skipCounter_;
    }
    if (df)
        dfDecoder();

    for (int c = 0; c < nch_; ++c) {
        Chan& ch = *ch_[static_cast<size_t>(c)];
        // the frame being enhanced: kLookahead frames back
        std::vector<cf>& target = ch.specY[static_cast<size_t>(ORDER - 1)];
        if (gains || zeros) {
            int k = 0;
            for (int b = 0; b < NERB; ++b)
                for (int j = 0; j < erbWidth_[static_cast<size_t>(b)]; ++j, ++k)
                    target[static_cast<size_t>(k)] *= mask_[static_cast<size_t>(b)];
        }
        std::array<cf, kBins> enh;
        std::copy(target.begin(), target.end(), enh.begin());
        if (df) {
            for (int f = 0; f < NDF; ++f) {
                cf a {};
                for (int i = 0; i < ORDER; ++i) {
                    const float* cc = &coefs_[static_cast<size_t>(f * 10 + i * 2)];
                    a += ch.specX[static_cast<size_t>(i)][static_cast<size_t>(f)] * cf(cc[0], cc[1]);
                }
                enh[static_cast<size_t>(f)] = a;
            }
        }
        const std::vector<cf>& noisy = ch.specX[static_cast<size_t>(std::max(ORDER, kLookahead) - kLookahead - 1)];
        if (gains && postFilter_) {
            const float eps = 1e-12f;
            for (int k = 0; k < kBins; ++k) {
                float m = std::abs(enh[static_cast<size_t>(k)]) / (std::abs(noisy[static_cast<size_t>(k)]) + eps);
                m = std::clamp(m, eps, 1.0f);
                const float ms = std::max(m * std::sin(static_cast<float>(kPi) * m / 2.0f), eps);
                const float r = m / ms;
                enh[static_cast<size_t>(k)] *= (1.0f + pfBeta_) / (1.0f + pfBeta_ * r * r);
            }
        }
        if (attenLim_ > 0.0f)
            for (int k = 0; k < kBins; ++k)
                enh[static_cast<size_t>(k)] = enh[static_cast<size_t>(k)] * (1.0f - attenLim_)
                    + noisy[static_cast<size_t>(k)] * attenLim_;
        synthesis(ch, enh.data(), out[c]);
    }
    return lsnr_;
}

} // namespace mend
