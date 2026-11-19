// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#include "Player.hpp"

#include "mend/audio/AudioDevices.hpp"
#include "mend/audio/Miniaudio.hpp"

#include <algorithm>
#include <cmath>

namespace mend::editor {

namespace {
constexpr unsigned kMaxBlock = 4096;
}

Player::Player()
    : device_(std::make_unique<ma_device>())
{
}

Player::~Player()
{
    stop();
    if (deviceOpen_) {
        stopping_.store(true);
        ma_device_uninit(device_.get());
    }
}

void Player::callback(ma_device* device, void* out, const void*, unsigned frames)
{
    static_cast<Player*>(device->pUserData)->render(static_cast<float*>(out), frames);
}

namespace {
void notification(const ma_device_notification* n)
{
    if (n->type == ma_device_notification_type_stopped)
        static_cast<Player*>(n->pDevice->pUserData)->onDeviceStopped();
}
} // namespace

void Player::onDeviceStopped()
{
    if (!stopping_.load() && playing_.load()) {
        playing_.store(false); // the output went away
        lost_.store(true);
    }
}

void Player::reloadSettings()
{
    stop();
    if (deviceOpen_) {
        stopping_.store(true);
        ma_device_uninit(device_.get());
        stopping_.store(false);
    }
    deviceOpen_ = false;
}

bool Player::takeLost()
{
    if (lost_.exchange(false))
        return true;
    // Some systems just stop calling back when a device disappears.
    const auto now = std::chrono::steady_clock::now();
    const size_t pos = readPos_.load();
    if (!playing_.load() || pos != watchPos_) {
        watchPos_ = pos;
        watchTime_ = now;
        return false;
    }
    if (now - watchTime_ > std::chrono::milliseconds(1500)) {
        stop();
        watchTime_ = now;
        return true;
    }
    return false;
}

unsigned Player::deviceRate() const
{
    return deviceOpen_ ? device_->sampleRate : 0;
}

double Player::latencyMs() const
{
    if (!deviceOpen_)
        return 0.0;
    const ma_device& d = *device_;
    const double rate = d.playback.internalSampleRate > 0 ? d.playback.internalSampleRate : d.sampleRate;
    return rate > 0 ? 1000.0 * d.playback.internalPeriodSizeInFrames * std::max(1u, d.playback.internalPeriods) / rate
                    : 0.0;
}

bool Player::play(const AudioBuffer* audio, size_t from, size_t to, bool preview, const ChainSettings& chain,
                  std::string& error)
{
    stop();
    if (audio == nullptr || audio->empty())
        return false;

    // (Re)open the device at the file's sample rate; miniaudio converts if
    // the hardware runs at another rate.
    if (!deviceOpen_ || deviceRate_ != audio->sampleRate) {
        if (deviceOpen_) {
            stopping_.store(true);
            ma_device_uninit(device_.get());
            stopping_.store(false);
        }
        deviceOpen_ = false;
        ma_context* ctx = mend::audio::sharedContext();
        if (ctx == nullptr) {
            error = "No audio output device was found.";
            return false;
        }
        // the output chosen in the Audio settings (by name), else the default
        const mend::audio::Settings settings = mend::audio::loadSettings();
        ma_device_id id {};
        const bool chosen = mend::audio::findDevice(ctx, false, settings.output, id);
        note_ = !settings.output.empty() && !chosen
            ? "\"" + settings.output + "\" isn't connected, so the system default output is in use."
            : std::string();
        ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
        cfg.playback.format = ma_format_f32;
        cfg.playback.channels = 2;
        cfg.playback.pDeviceID = chosen ? &id : nullptr;
        cfg.sampleRate = static_cast<ma_uint32>(std::lround(audio->sampleRate));
        cfg.dataCallback = &Player::callback;
        cfg.notificationCallback = &notification;
        cfg.pUserData = this;
        cfg.periodSizeInFrames = std::clamp(settings.bufferSize, 64u, 4096u);
        ma_result r = ma_device_init(ctx, &cfg, device_.get());
        if (r != MA_SUCCESS && chosen) { // the chosen output won't open: try the default
            cfg.playback.pDeviceID = nullptr;
            note_ = "\"" + settings.output + "\" couldn't be opened, so the system default output is in use.";
            r = ma_device_init(ctx, &cfg, device_.get());
        }
        if (r != MA_SUCCESS) {
            error = "Couldn't open the audio output device.";
            return false;
        }
        deviceOpen_ = true;
        deviceRate_ = audio->sampleRate;
        outputName_ = device_->playback.name;
        bufferSize_ = cfg.periodSizeInFrames;
    }

    audio_ = audio;
    const size_t n = audio->numFrames();
    from = std::min(from, n);
    endPos_ = (to == 0 || to > n) ? n : to;
    readPos_.store(from);
    watchPos_ = from;
    watchTime_ = std::chrono::steady_clock::now();

    // Preview chain: build processors now (allocation stays off the audio thread).
    preview_ = preview && anyEnabled(chain);
    active_.clear();
    procs_.clear();
    latency_ = 0;
    if (preview_) {
        for (ToolId id : chainOrder()) {
            if (!chain[static_cast<int>(id)].enabled)
                continue;
            auto p = makeProcessor(id);
            p->prepare(audio->sampleRate, audio->numChannels(), static_cast<int>(kMaxBlock));
            p->apply(chain[static_cast<int>(id)]);
            latency_ += p->latency();
            active_.push_back(id);
            procs_.push_back(std::move(p));
        }
    }
    tailLeft_ = static_cast<size_t>(latency_);
    bufA_.assign(audio->numChannels(), std::vector<float>(kMaxBlock));
    bufB_.assign(audio->numChannels(), std::vector<float>(kMaxBlock));
    {
        std::lock_guard<std::mutex> l(settingsMutex_);
        pendingDirty_ = false;
    }

    playing_.store(true);
    if (ma_device_start(device_.get()) != MA_SUCCESS) {
        playing_.store(false);
        if (!retrying_) { // e.g. the device went away while idle: open it afresh
            retrying_ = true;
            reloadSettings();
            const bool ok = play(audio, from, to, preview, chain, error);
            retrying_ = false;
            return ok;
        }
        error = "Couldn't start audio playback.";
        return false;
    }
    return true;
}

void Player::stop()
{
    if (deviceOpen_ && ma_device_is_started(device_.get())) {
        stopping_.store(true);
        ma_device_stop(device_.get());
        stopping_.store(false);
    }
    playing_.store(false);
}

size_t Player::position() const noexcept
{
    const size_t p = readPos_.load();
    return p > static_cast<size_t>(latency_) ? p - latency_ : 0;
}

const ToolProcessor* Player::processor(ToolId id) const
{
    if (!playing_.load() || !preview_)
        return nullptr;
    for (size_t i = 0; i < active_.size() && i < procs_.size(); ++i)
        if (active_[i] == id)
            return procs_[i].get();
    return nullptr;
}

void Player::updateSettings(const ChainSettings& chain)
{
    std::lock_guard<std::mutex> l(settingsMutex_);
    pending_ = chain;
    pendingDirty_ = true;
}

void Player::takePeaks(float& left, float& right)
{
    left = peakL_.exchange(0.0f);
    right = peakR_.exchange(0.0f);
}

void Player::render(float* out, unsigned frames)
{
    std::fill(out, out + frames * 2, 0.0f);
    if (!playing_.load() || audio_ == nullptr)
        return;

    // pick up new settings without ever blocking
    if (settingsMutex_.try_lock()) {
        if (pendingDirty_) {
            for (size_t i = 0; i < procs_.size(); ++i)
                procs_[i]->apply(pending_[static_cast<int>(active_[i])]);
            pendingDirty_ = false;
        }
        settingsMutex_.unlock();
    }

    const int nch = audio_->numChannels();
    unsigned done = 0;
    float pl = peakL_.load(), pr = peakR_.load();
    while (done < frames) {
        const unsigned m = std::min(frames - done, kMaxBlock);
        size_t pos = readPos_.load();
        unsigned avail = 0;
        for (int c = 0; c < nch; ++c) {
            avail = 0;
            for (unsigned i = 0; i < m; ++i) {
                const size_t k = pos + i;
                const bool inside = k < endPos_;
                bufA_[c][i] = inside ? audio_->channels[c][k] : 0.0f;
                if (inside)
                    ++avail;
            }
        }
        if (!procs_.empty()) {
            float* ptrs[16];
            const float* cptrs[16];
            const int cc = std::min(nch, 16);
            for (int c = 0; c < cc; ++c) {
                ptrs[c] = bufA_[c].data();
                cptrs[c] = bufA_[c].data();
            }
            for (auto& p : procs_)
                p->process(cptrs, ptrs, cc, static_cast<int>(m)); // in place
        }
        for (unsigned i = 0; i < m; ++i) {
            const float l = bufA_[0][i];
            const float r = nch > 1 ? bufA_[1][i] : l;
            out[(done + i) * 2] = l;
            out[(done + i) * 2 + 1] = r;
            pl = std::max(pl, std::abs(l));
            pr = std::max(pr, std::abs(r));
        }
        readPos_.store(pos + m);
        done += m;

        // past the end: let the preview's latency drain, then stop
        if (avail < m) {
            const unsigned silent = m - avail;
            tailLeft_ = tailLeft_ > silent ? tailLeft_ - silent : 0;
            if (tailLeft_ == 0) {
                playing_.store(false);
                break;
            }
        }
    }
    peakL_.store(pl);
    peakR_.store(pr);
}

} // namespace mend::editor
