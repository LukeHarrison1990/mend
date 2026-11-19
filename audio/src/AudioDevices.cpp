// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#include "mend/audio/AudioDevices.hpp"
#include "mend/audio/Miniaudio.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <sys/stat.h>
#endif

namespace mend::audio {

// ---------------------------------------------------------------------------
// Settings file

namespace {

#ifdef _WIN32
std::wstring widen(const std::string& s)
{
    if (s.empty())
        return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}
std::string narrow(const wchar_t* w)
{
    if (w == nullptr || *w == 0)
        return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n > 0 ? n - 1 : 0), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}
#endif

std::string configDir()
{
#ifdef _WIN32
    const std::string appData = narrow(_wgetenv(L"APPDATA"));
    return appData.empty() ? std::string() : appData + "\\Mend";
#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    return home ? std::string(home) + "/Library/Application Support/Mend" : std::string();
#else
    if (const char* x = std::getenv("XDG_CONFIG_HOME"); x != nullptr && *x == '/')
        return std::string(x) + "/mend";
    const char* home = std::getenv("HOME");
    return home ? std::string(home) + "/.config/mend" : std::string();
#endif
}

void makeDirs(const std::string& dir)
{
    // create each level in turn (existing ones are fine)
    for (size_t i = 1; i <= dir.size(); ++i) {
        if (i != dir.size() && dir[i] != '/' && dir[i] != '\\')
            continue;
        const std::string part = dir.substr(0, i);
#ifdef _WIN32
        if (part.size() <= 3 && part.find(':') != std::string::npos)
            continue; // "C:\"
        CreateDirectoryW(widen(part).c_str(), nullptr);
#else
        mkdir(part.c_str(), 0755);
#endif
    }
}

std::FILE* openUtf8(const std::string& path, const char* mode)
{
#ifdef _WIN32
    return _wfopen(widen(path).c_str(), widen(mode).c_str());
#else
    return std::fopen(path.c_str(), mode);
#endif
}

std::string trim(std::string s)
{
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\t'))
        s.pop_back();
    size_t a = 0;
    while (a < s.size() && (s[a] == ' ' || s[a] == '\t'))
        ++a;
    return s.substr(a);
}

} // namespace

std::string settingsPath()
{
    const std::string dir = configDir();
    if (dir.empty())
        return {};
#ifdef _WIN32
    return dir + "\\audio.ini";
#else
    return dir + "/audio.ini";
#endif
}

Settings loadSettings()
{
    Settings s;
    const std::string path = settingsPath();
    std::FILE* f = path.empty() ? nullptr : openUtf8(path, "rb");
    if (f == nullptr)
        return s;
    char line[1024];
    while (std::fgets(line, sizeof(line), f) != nullptr) {
        const std::string l = trim(line);
        if (l.empty() || l[0] == '#' || l[0] == ';')
            continue;
        const size_t eq = l.find('=');
        if (eq == std::string::npos)
            continue;
        const std::string key = trim(l.substr(0, eq)), val = trim(l.substr(eq + 1));
        if (key == "input_on")
            s.inputOn = val == "1" || val == "yes" || val == "true";
        else if (key == "input")
            s.input = val;
        else if (key == "input_channels") {
            int a = 0, b = 0;
            if (std::sscanf(val.c_str(), "%d + %d", &a, &b) == 2 || std::sscanf(val.c_str(), "%d+%d", &a, &b) == 2) {
                s.inputLeft = std::max(1, a);
                s.inputRight = std::max(1, b);
            } else if (std::sscanf(val.c_str(), "%d", &a) == 1) {
                s.inputLeft = s.inputRight = std::max(1, a);
            }
        } else if (key == "output")
            s.output = val;
        else if (key == "sample_rate")
            s.sampleRate = static_cast<unsigned>(std::strtoul(val.c_str(), nullptr, 10));
        else if (key == "buffer_size") {
            const unsigned b = static_cast<unsigned>(std::strtoul(val.c_str(), nullptr, 10));
            if (b >= 16 && b <= 8192)
                s.bufferSize = b;
        }
    }
    std::fclose(f);
    return s;
}

bool saveSettings(const Settings& s)
{
    const std::string dir = configDir();
    if (dir.empty())
        return false;
    makeDirs(dir);
    std::FILE* f = openUtf8(settingsPath(), "wb");
    if (f == nullptr)
        return false;
    std::fprintf(f, "# Mend audio settings (shared by the standalone apps and the editor).\n");
    std::fprintf(f, "# An empty device means the system default.\n");
    std::fprintf(f, "input_on=%d\n", s.inputOn ? 1 : 0);
    std::fprintf(f, "input=%s\n", s.input.c_str());
    if (s.inputLeft == s.inputRight)
        std::fprintf(f, "input_channels=%d\n", s.inputLeft);
    else
        std::fprintf(f, "input_channels=%d+%d\n", s.inputLeft, s.inputRight);
    std::fprintf(f, "output=%s\n", s.output.c_str());
    std::fprintf(f, "sample_rate=%u\n", s.sampleRate);
    std::fprintf(f, "buffer_size=%u\n", s.bufferSize);
    return std::fclose(f) == 0;
}

std::vector<ChannelChoice> channelChoices(unsigned channels)
{
    std::vector<ChannelChoice> c;
    const int n = static_cast<int>(std::max(1u, channels));
    char buf[48];
    for (int i = 1; i + 1 <= n; i += 2) {
        std::snprintf(buf, sizeof(buf), "Inputs %d + %d", i, i + 1);
        c.push_back({ i, i + 1, buf });
    }
    for (int i = 1; i <= n; ++i) {
        std::snprintf(buf, sizeof(buf), n == 1 ? "Input %d (mono)" : "Input %d (mono to both sides)", i);
        c.push_back({ i, i, buf });
    }
    return c;
}

const std::vector<unsigned>& sampleRateChoices()
{
    static const std::vector<unsigned> r { 0, 44100, 48000, 88200, 96000 };
    return r;
}

const std::vector<unsigned>& bufferSizeChoices()
{
    static const std::vector<unsigned> b { 64, 128, 256, 512, 1024, 2048 };
    return b;
}

// ---------------------------------------------------------------------------
// miniaudio context and device lookup

ma_context* sharedContext()
{
    static ma_context ctx;
    static bool ok = false;
    static std::once_flag once;
    std::call_once(once, [] {
        ma_context_config cfg = ma_context_config_init();
        ok = ma_context_init(nullptr, 0, &cfg, &ctx) == MA_SUCCESS;
        if (ok && ctx.backend == ma_backend_null) { // silent fallback: no real audio
            ma_context_uninit(&ctx);
            ok = false;
        }
    });
    return ok ? &ctx : nullptr;
}

namespace {

std::mutex& listMutex()
{
    static std::mutex m;
    return m;
}

DeviceList listDevices(ma_context* ctx)
{
    DeviceList d;
    if (ctx == nullptr)
        return d;
    d.backend = ma_get_backend_name(ctx->backend);
    std::lock_guard<std::mutex> l(listMutex());
    ma_device_info *play = nullptr, *cap = nullptr;
    ma_uint32 nPlay = 0, nCap = 0;
    if (ma_context_get_devices(ctx, &play, &nPlay, &cap, &nCap) != MA_SUCCESS)
        return d;
    auto add = [](std::vector<std::string>& list, std::string& def, const ma_device_info& info) {
        std::string name = info.name;
        if (name.empty())
            return;
        if (std::find(list.begin(), list.end(), name) == list.end())
            list.push_back(name);
        if (info.isDefault && def.empty())
            def = name;
    };
    for (ma_uint32 i = 0; i < nCap; ++i)
        add(d.inputs, d.defaultInput, cap[i]);
    for (ma_uint32 i = 0; i < nPlay; ++i)
        add(d.outputs, d.defaultOutput, play[i]);
    return d;
}

} // namespace

DeviceList listDevices()
{
    return listDevices(sharedContext());
}

bool findDevice(ma_context* ctx, bool capture, const std::string& name, ma_device_id& id)
{
    if (ctx == nullptr || name.empty())
        return false;
    std::lock_guard<std::mutex> l(listMutex());
    ma_device_info *play = nullptr, *cap = nullptr;
    ma_uint32 nPlay = 0, nCap = 0;
    if (ma_context_get_devices(ctx, &play, &nPlay, &cap, &nCap) != MA_SUCCESS)
        return false;
    const ma_device_info* list = capture ? cap : play;
    const ma_uint32 n = capture ? nCap : nPlay;
    for (ma_uint32 i = 0; i < n; ++i) {
        if (name == list[i].name) {
            id = list[i].id;
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Duplex stream

struct Duplex::Impl {
    ma_context* ctx = nullptr;
    ma_device dev {};
    bool devInit = false;

    // read on the audio thread; set only while the device is stopped
    Callback cb = nullptr;
    void* user = nullptr;
    unsigned block = 0;
    bool input = false;
    unsigned inCh = 0;
    unsigned left = 0, right = 0; // 0-based device channels
    std::vector<float> inL, inR, outL, outR;

    std::atomic<bool> stopping { false };
    std::atomic<bool> lost { false };
    std::atomic<unsigned> callbacks { 0 }; // counts audio callbacks (watchdog)

    static void data(ma_device* d, void* pOut, const void* pIn, ma_uint32 frames)
    {
        Impl* im = static_cast<Impl*>(d->pUserData);
        im->callbacks.fetch_add(1, std::memory_order_relaxed);
        const float* in = static_cast<const float*>(pIn);
        float* out = static_cast<float*>(pOut);
        ma_uint32 done = 0;
        while (done < frames) {
            const unsigned n = std::min<unsigned>(im->block, frames - done);
            if (im->input && in != nullptr) {
                const float* src = in + static_cast<size_t>(done) * im->inCh;
                for (unsigned i = 0; i < n; ++i) {
                    im->inL[i] = src[static_cast<size_t>(i) * im->inCh + im->left];
                    im->inR[i] = src[static_cast<size_t>(i) * im->inCh + im->right];
                }
            } else {
                std::fill(im->inL.begin(), im->inL.begin() + n, 0.0f);
                std::fill(im->inR.begin(), im->inR.begin() + n, 0.0f);
            }
            const float* ins[2] = { im->inL.data(), im->inR.data() };
            float* outs[2] = { im->outL.data(), im->outR.data() };
            if (im->cb != nullptr) {
                im->cb(im->user, ins, outs, n);
            } else {
                std::fill(im->outL.begin(), im->outL.begin() + n, 0.0f);
                std::fill(im->outR.begin(), im->outR.begin() + n, 0.0f);
            }
            if (out != nullptr) {
                float* dst = out + static_cast<size_t>(done) * 2;
                for (unsigned i = 0; i < n; ++i) {
                    dst[2 * i] = im->outL[i];
                    dst[2 * i + 1] = im->outR[i];
                }
            }
            done += n;
        }
    }

    static void notify(const ma_device_notification* n)
    {
        Impl* im = static_cast<Impl*>(n->pDevice->pUserData);
        if (n->type == ma_device_notification_type_stopped && !im->stopping.load())
            im->lost.store(true); // e.g. the device was unplugged
    }
};

Duplex::Duplex()
    : impl_(std::make_unique<Impl>())
{
    impl_->ctx = sharedContext();
}

Duplex::~Duplex()
{
    close();
}

bool Duplex::ready() const
{
    return impl_->ctx != nullptr;
}

DeviceList Duplex::devices()
{
    return listDevices(impl_->ctx);
}

double Duplex::latencyMs() const
{
    if (!open_ || sampleRate_ == 0)
        return 0.0;
    const ma_device& d = impl_->dev;
    double frames = static_cast<double>(d.playback.internalPeriodSizeInFrames) * std::max(1u, d.playback.internalPeriods);
    if (inputActive_)
        frames += static_cast<double>(d.capture.internalPeriodSizeInFrames);
    // internal sizes are at the device's own rate
    const double rate = d.playback.internalSampleRate > 0 ? d.playback.internalSampleRate : sampleRate_;
    return 1000.0 * frames / rate;
}

bool Duplex::openExact(const Settings& s, bool inputOn, const std::string& in, const std::string& out,
                       std::string& err)
{
    Impl& im = *impl_;
    ma_device_id inId {}, outId {};
    const bool haveIn = inputOn && findDevice(im.ctx, true, in, inId);
    const bool haveOut = findDevice(im.ctx, false, out, outId);
    if ((inputOn && !in.empty() && !haveIn) || (!out.empty() && !haveOut)) {
        err = "device missing";
        return false; // gone since the list was made: the caller falls back
    }

    ma_device_config cfg = ma_device_config_init(inputOn ? ma_device_type_duplex : ma_device_type_playback);
    cfg.playback.format = ma_format_f32;
    cfg.playback.channels = 2;
    cfg.playback.pDeviceID = haveOut ? &outId : nullptr;
    cfg.capture.format = ma_format_f32;
    cfg.capture.channels = 0; // the device's own count; we pick the channels
    cfg.capture.pDeviceID = haveIn ? &inId : nullptr;
    cfg.sampleRate = s.sampleRate;
    cfg.periodSizeInFrames = std::clamp(s.bufferSize, 16u, 8192u);
    cfg.performanceProfile = ma_performance_profile_low_latency;
    cfg.dataCallback = &Impl::data;
    cfg.notificationCallback = &Impl::notify;
    cfg.pUserData = &im;

    const ma_result r = ma_device_init(im.ctx, &cfg, &im.dev);
    if (r != MA_SUCCESS) {
        err = ma_result_description(r);
        return false;
    }
    im.devInit = true;
    im.input = inputOn;
    im.inCh = inputOn ? im.dev.capture.channels : 0;
    if (inputOn && im.inCh == 0) {
        ma_device_uninit(&im.dev);
        im.devInit = false;
        err = "the input has no channels";
        return false;
    }
    const unsigned maxCh = std::max(1u, im.inCh);
    im.left = static_cast<unsigned>(std::clamp(s.inputLeft, 1, static_cast<int>(maxCh)) - 1);
    im.right = static_cast<unsigned>(std::clamp(s.inputRight, 1, static_cast<int>(maxCh)) - 1);
    if (im.inCh == 1)
        im.left = im.right = 0;
    im.block = std::clamp(s.bufferSize, 16u, 8192u);
    im.inL.assign(im.block, 0.0f);
    im.inR.assign(im.block, 0.0f);
    im.outL.assign(im.block, 0.0f);
    im.outR.assign(im.block, 0.0f);
    im.cb = cb_;
    im.user = user_;
    im.lost.store(false);

    inputActive_ = inputOn;
    inputChannels_ = im.inCh;
    sampleRate_ = im.dev.sampleRate;
    maxBlock_ = im.block;
    inputName_ = inputOn ? std::string(im.dev.capture.name) : std::string();
    outputName_ = im.dev.playback.name;
    open_ = true;
    return true;
}

bool Duplex::open(const Settings& s, Callback cb, void* user)
{
    close();
    wanted_ = s;
    cb_ = cb;
    user_ = user;
    note_.clear();
    usingFallback_ = false;
    if (impl_->ctx == nullptr) {
        note_ = "No audio system was found.";
        return false;
    }

    std::string in, out, err;
    for (int attempt = 0; attempt < 2; ++attempt) { // again if a device vanishes meanwhile
        ma_device_id id {};
        in = s.input;
        out = s.output;
        note_.clear();
        if (s.inputOn && !in.empty() && !findDevice(impl_->ctx, true, in, id)) {
            note_ = "\"" + in + "\" isn't connected, so the system default input is in use.";
            in.clear();
            usingFallback_ = true;
        }
        if (!out.empty() && !findDevice(impl_->ctx, false, out, id)) {
            note_ = (note_.empty() ? "" : note_ + " ") + "\"" + out
                + "\" isn't connected, so the system default output is in use.";
            out.clear();
            usingFallback_ = true;
        }
        err.clear();
        if (openExact(s, s.inputOn, in, out, err))
            return true;
        if (err != "device missing")
            break;
    }
    if (s.inputOn) { // try without the input
        std::string err2;
        if (openExact(s, false, in, out, err2)) {
            note_ = "The input couldn't be opened (" + err + "), so input is off.";
            return true;
        }
    }
    if (!in.empty() || !out.empty() || s.sampleRate != 0) { // last resort: defaults, device rate
        Settings d = s;
        d.sampleRate = 0;
        std::string err3;
        if (openExact(d, false, std::string(), std::string(), err3)) {
            note_ = "The chosen devices couldn't be opened (" + err + "), so the system default output is in use.";
            return true;
        }
    }
    note_ = "Audio couldn't be opened: " + err + ".";
    return false;
}

void Duplex::close()
{
    stop();
    if (impl_->devInit) {
        impl_->stopping.store(true);
        ma_device_uninit(&impl_->dev);
        impl_->stopping.store(false);
        impl_->devInit = false;
    }
    open_ = false;
    inputActive_ = false;
}

bool Duplex::start()
{
    if (!open_)
        return false;
    impl_->stopping.store(false);
    started_ = ma_device_start(&impl_->dev) == MA_SUCCESS;
    return started_;
}

void Duplex::stop()
{
    if (open_ && ma_device_is_started(&impl_->dev)) {
        impl_->stopping.store(true);
        ma_device_stop(&impl_->dev);
        impl_->stopping.store(false);
    }
    started_ = false;
}

bool Duplex::idle(bool restart)
{
    if (impl_->ctx == nullptr)
        return false;
    const bool wasStarted = started_ || impl_->lost.load();
    bool reopen = impl_->lost.exchange(false);
    // Every couple of seconds (idle() runs at roughly 30 Hz): has a wanted
    // device come back, or gone without the system saying, or has the
    // stream silently stalled?
    if (!reopen && ++idleTicks_ >= 60) {
        idleTicks_ = 0;
        const unsigned calls = impl_->callbacks.load(std::memory_order_relaxed);
        if (started_ && open_ && calls == lastCallbacks_)
            reopen = true;
        lastCallbacks_ = calls;
        if (!reopen && open_) {
            ma_device_id id {};
            const bool inThere = !wanted_.inputOn || wanted_.input.empty() || findDevice(impl_->ctx, true, wanted_.input, id);
            const bool outThere = wanted_.output.empty() || findDevice(impl_->ctx, false, wanted_.output, id);
            if (usingFallback_)
                reopen = inThere && outThere; // back again
            else
                reopen = !inThere || !outThere; // gone
        }
    }
    if (!reopen)
        return false;
    const Settings s = wanted_;
    open(s, cb_, user_);
    if (wasStarted && restart)
        start();
    lastCallbacks_ = impl_->callbacks.load(std::memory_order_relaxed);
    idleTicks_ = 0;
    return true;
}

} // namespace mend::audio
