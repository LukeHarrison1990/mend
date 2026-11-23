// mend-denoise: run a file through Mend's De-noise engine from the command line.
//
//   mend-denoise in.wav out.wav [options]
//     --mode any|voice        engine (default any)
//     --reduction DB          Any sound: maximum reduction (default 12)
//     --threshold DB          Any sound: threshold (default 0)
//     --smoothing PCT         Any sound: artefact smoothing 0-100 (default 50)
//     --learn SECONDS         Any sound: learn the noise from the first SECONDS
//                             of the file instead of tracking it (default 0 = adaptive)
//     --voice-reduction DB    Voice: reduction, 0-60, or 100 for full (default 100)
//
// Reads 16/24/32-bit PCM or 32-bit float WAV, writes 32-bit float WAV of the
// same length and channel count, latency removed. Used by the benchmark in
// this folder; handy on its own for batch jobs.
#include "mend/DeNoiseEngine.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct Audio {
    int channels = 0;
    double rate = 0.0;
    std::vector<std::vector<float>> data;
};

std::uint32_t le32(const unsigned char* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (std::uint32_t(p[3]) << 24); }
std::uint16_t le16(const unsigned char* p) { return std::uint16_t(p[0] | (p[1] << 8)); }

bool readWav(const char* path, Audio& a)
{
    FILE* f = std::fopen(path, "rb");
    if (!f)
        return false;
    std::vector<unsigned char> buf;
    unsigned char tmp[65536];
    size_t n;
    while ((n = std::fread(tmp, 1, sizeof tmp, f)) > 0)
        buf.insert(buf.end(), tmp, tmp + n);
    std::fclose(f);
    if (buf.size() < 12 || std::memcmp(buf.data(), "RIFF", 4) || std::memcmp(buf.data() + 8, "WAVE", 4))
        return false;
    int fmt = 0, bits = 0;
    size_t pos = 12;
    while (pos + 8 <= buf.size()) {
        const unsigned char* c = buf.data() + pos;
        const size_t len = le32(c + 4);
        const unsigned char* d = c + 8;
        if (!std::memcmp(c, "fmt ", 4) && len >= 16) {
            fmt = le16(d);
            a.channels = le16(d + 2);
            a.rate = le32(d + 4);
            bits = le16(d + 14);
            if (fmt == 0xFFFE && len >= 26)
                fmt = le16(d + 24); // WAVE_FORMAT_EXTENSIBLE: sub-format
        } else if (!std::memcmp(c, "data", 4) && a.channels > 0) {
            const int bytes = bits / 8;
            const size_t avail = std::min(len, buf.size() - pos - 8);
            const size_t frames = avail / (size_t(bytes) * a.channels);
            a.data.assign(a.channels, std::vector<float>(frames));
            for (size_t i = 0; i < frames; ++i)
                for (int ch = 0; ch < a.channels; ++ch) {
                    const unsigned char* s = d + (i * a.channels + ch) * bytes;
                    float v = 0.0f;
                    if (fmt == 3 && bits == 32) {
                        std::uint32_t u = le32(s);
                        std::memcpy(&v, &u, 4);
                    } else if (fmt == 1 && bits == 16) {
                        v = std::int16_t(le16(s)) / 32768.0f;
                    } else if (fmt == 1 && bits == 24) {
                        std::int32_t x = (s[0] << 8) | (s[1] << 16) | (std::int32_t(s[2]) << 24);
                        v = (x >> 8) / 8388608.0f;
                    } else if (fmt == 1 && bits == 32) {
                        v = std::int32_t(le32(s)) / 2147483648.0f;
                    } else {
                        return false;
                    }
                    a.data[ch][i] = v;
                }
            return true;
        }
        pos += 8 + len + (len & 1);
    }
    return false;
}

void put32(std::vector<unsigned char>& b, std::uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back((v >> (8 * i)) & 255); }
void put16(std::vector<unsigned char>& b, std::uint16_t v) { b.push_back(v & 255); b.push_back(v >> 8); }

bool writeWav(const char* path, const Audio& a)
{
    const size_t frames = a.data.empty() ? 0 : a.data[0].size();
    const std::uint32_t dataBytes = std::uint32_t(frames * a.channels * 4);
    std::vector<unsigned char> b;
    b.insert(b.end(), { 'R', 'I', 'F', 'F' });
    put32(b, 36 + dataBytes);
    b.insert(b.end(), { 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ' });
    put32(b, 16);
    put16(b, 3);
    put16(b, std::uint16_t(a.channels));
    put32(b, std::uint32_t(a.rate));
    put32(b, std::uint32_t(a.rate * a.channels * 4));
    put16(b, std::uint16_t(a.channels * 4));
    put16(b, 32);
    b.insert(b.end(), { 'd', 'a', 't', 'a' });
    put32(b, dataBytes);
    for (size_t i = 0; i < frames; ++i)
        for (int ch = 0; ch < a.channels; ++ch) {
            std::uint32_t u;
            std::memcpy(&u, &a.data[ch][i], 4);
            put32(b, u);
        }
    FILE* f = std::fopen(path, "wb");
    if (!f)
        return false;
    const bool ok = std::fwrite(b.data(), 1, b.size(), f) == b.size();
    return std::fclose(f) == 0 && ok;
}

int usage()
{
    std::fprintf(stderr, "usage: mend-denoise in.wav out.wav [--mode any|voice] [--reduction dB] [--threshold dB]\n"
                         "                    [--smoothing 0-100] [--learn seconds] [--voice-reduction dB]\n");
    return 2;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 3)
        return usage();
    mend::SpectralDenoise::Settings s;
    s.adaptive = true;
    double learn = 0.0;
    float voiceDb = 100.0f;
    auto mode = mend::DeNoiseEngine::Mode::AnySound;
    for (int i = 3; i < argc; ++i) {
        const std::string k = argv[i];
        if (i + 1 >= argc)
            return usage();
        const char* v = argv[++i];
        if (k == "--mode")
            mode = std::string(v) == "voice" ? mend::DeNoiseEngine::Mode::Voice : mend::DeNoiseEngine::Mode::AnySound;
        else if (k == "--reduction")
            s.reductionDb = std::atof(v);
        else if (k == "--threshold")
            s.thresholdDb = std::atof(v);
        else if (k == "--smoothing")
            s.smoothing = std::atof(v);
        else if (k == "--learn")
            learn = std::atof(v);
        else if (k == "--voice-reduction")
            voiceDb = static_cast<float>(std::atof(v));
        else
            return usage();
    }
    Audio in;
    if (!readWav(argv[1], in) || in.channels < 1 || in.rate <= 0) {
        std::fprintf(stderr, "mend-denoise: can't read %s (16/24/32-bit PCM or float WAV)\n", argv[1]);
        return 1;
    }
    const int nch = in.channels;
    const size_t frames = in.data[0].size();
    constexpr int kBlock = 512;

    mend::DeNoiseEngine eng;
    eng.prepare(in.rate, nch, kBlock);
    eng.setMode(mode);
    eng.setVoiceReductionDb(voiceDb);

    std::vector<std::vector<float>> scratch(nch, std::vector<float>(kBlock));
    std::vector<const float*> ip(nch);
    std::vector<float*> op(nch);
    auto run = [&](const std::vector<std::vector<float>>& src, std::vector<std::vector<float>>* dst, size_t start,
                   size_t count) {
        for (size_t p = 0; p < count; p += kBlock) {
            const int m = static_cast<int>(std::min<size_t>(kBlock, count - p));
            for (int ch = 0; ch < nch; ++ch) {
                ip[ch] = src[ch].data() + start + p;
                op[ch] = dst ? (*dst)[ch].data() + start + p : scratch[ch].data();
            }
            eng.process(ip.data(), op.data(), nch, m);
        }
    };

    if (mode == mend::DeNoiseEngine::Mode::AnySound && learn > 0.0) {
        auto l = s;
        l.learn = true;
        l.adaptive = false;
        eng.setSpectralSettings(l);
        run(in.data, nullptr, 0, std::min(frames, static_cast<size_t>(learn * in.rate)));
        s.adaptive = false;
        eng.setSpectralSettings(s);
        run(in.data, nullptr, 0, std::min<size_t>(frames, kBlock)); // ends learning: the profile is kept
        if (!eng.spectral().hasLearnedProfile())
            std::fprintf(stderr, "mend-denoise: warning: no noise profile learned\n");
        eng.reset();
    } else {
        eng.setSpectralSettings(s);
    }

    const int lat = eng.latency();
    std::vector<std::vector<float>> src(nch), dst(nch);
    for (int ch = 0; ch < nch; ++ch) {
        src[ch] = in.data[ch];
        src[ch].resize(frames + lat, 0.0f);
        dst[ch].assign(frames + lat, 0.0f);
    }
    run(src, &dst, 0, frames + lat);

    Audio out;
    out.channels = nch;
    out.rate = in.rate;
    out.data.resize(nch);
    for (int ch = 0; ch < nch; ++ch)
        out.data[ch].assign(dst[ch].begin() + lat, dst[ch].begin() + lat + static_cast<long>(frames));
    if (!writeWav(argv[2], out)) {
        std::fprintf(stderr, "mend-denoise: can't write %s\n", argv[2]);
        return 1;
    }
    return 0;
}
