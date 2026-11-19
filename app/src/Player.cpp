// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#include "Player.hpp"

#include "miniaudio.h"

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
    if (deviceOpen_)
        ma_device_uninit(device_.get());
}

void Player::callback(ma_device* device, void* out, const void*, unsigned frames)
{
    static_cast<Player*>(device->pUserData)->render(static_cast<float*>(out), frames);
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
        if (deviceOpen_)
            ma_device_uninit(device_.get());
        deviceOpen_ = false;
        ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
        cfg.playback.format = ma_format_f32;
        cfg.playback.channels = 2;
        cfg.sampleRate = static_cast<ma_uint32>(std::lround(audio->sampleRate));
        cfg.dataCallback = &Player::callback;
        cfg.pUserData = this;
        cfg.periodSizeInFrames = 512;
        if (ma_device_init(nullptr, &cfg, device_.get()) != MA_SUCCESS) {
            error = "Couldn't open the audio output device.";
            return false;
        }
        if (device_->pContext != nullptr && device_->pContext->backend == ma_backend_null) {
            // miniaudio's silent fallback: there is no real output to play to
            ma_device_uninit(device_.get());
            error = "No audio output device was found.";
            return false;
        }
        deviceOpen_ = true;
        deviceRate_ = audio->sampleRate;
    }

    audio_ = audio;
    const size_t n = audio->numFrames();
    from = std::min(from, n);
    endPos_ = (to == 0 || to > n) ? n : to;
    readPos_.store(from);

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
        error = "Couldn't start audio playback.";
        return false;
    }
    return true;
}

void Player::stop()
{
    if (deviceOpen_ && ma_device_is_started(device_.get()))
        ma_device_stop(device_.get());
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
