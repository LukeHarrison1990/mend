// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#include "Renderer.hpp"

#include "mend/Biquad.hpp"
#include "mend/DeHum.hpp"
#include "mend/Offline.hpp"

#include <algorithm>
#include <cmath>

namespace mend::editor {

namespace {
constexpr double kPreRollSeconds = 3.0;
constexpr double kPostRollSeconds = 0.25;
constexpr double kEdgeFadeSeconds = 0.010;
constexpr int kBlock = 4096;

using Channels = std::vector<std::vector<float>>;

// Run one processor over `buf` in place, compensating its latency so the
// output is aligned with the input. Returns false if cancelled.
bool runAligned(ToolProcessor& p, Channels& buf, std::atomic<bool>* cancel, std::atomic<float>* progress,
                float progressFrom, float progressSpan)
{
    const int nch = static_cast<int>(buf.size());
    const size_t n = buf[0].size();
    const size_t L = static_cast<size_t>(std::max(0, p.latency()));
    Channels out(nch, std::vector<float>(n, 0.0f));
    std::vector<std::vector<float>> inBlock(nch, std::vector<float>(kBlock)), outBlock(nch, std::vector<float>(kBlock));
    std::vector<const float*> ip(nch);
    std::vector<float*> op(nch);

    const size_t total = n + L;
    for (size_t pos = 0; pos < total; pos += kBlock) {
        if (cancel && cancel->load())
            return false;
        const int m = static_cast<int>(std::min<size_t>(kBlock, total - pos));
        for (int c = 0; c < nch; ++c) {
            for (int i = 0; i < m; ++i) {
                const size_t k = pos + i;
                inBlock[c][i] = k < n ? buf[c][k] : 0.0f;
            }
            ip[c] = inBlock[c].data();
            op[c] = outBlock[c].data();
        }
        p.process(ip.data(), op.data(), nch, m);
        for (int c = 0; c < nch; ++c)
            for (int i = 0; i < m; ++i) {
                const size_t k = pos + i;
                if (k >= L && k - L < n)
                    out[c][k - L] = outBlock[c][i];
            }
        if (progress)
            progress->store(progressFrom + progressSpan * static_cast<float>(pos) / static_cast<float>(total));
    }
    buf.swap(out);
    return true;
}

// Run a processor over the time-reversed audio, then reverse back.
bool runReversed(ToolProcessor& p, Channels& buf, std::atomic<bool>* cancel, std::atomic<float>* progress,
                 float from, float span)
{
    for (auto& c : buf)
        std::reverse(c.begin(), c.end());
    const bool ok = runAligned(p, buf, cancel, progress, from, span);
    for (auto& c : buf)
        std::reverse(c.begin(), c.end());
    return ok;
}

// Find the hum in the material (De-hum's detector, run over all of it).
bool detectHum(const Channels& buf, double fs, double& freq)
{
    DeHum d;
    d.prepare(fs, kBlock, static_cast<int>(buf.size()));
    d.setSettings(DeHum::Settings {}); // auto
    const int nch = static_cast<int>(buf.size());
    std::vector<std::vector<float>> o(nch, std::vector<float>(kBlock));
    std::vector<const float*> ip(nch);
    std::vector<float*> op(nch);
    for (size_t pos = 0; pos < buf[0].size(); pos += kBlock) {
        const int m = static_cast<int>(std::min<size_t>(kBlock, buf[0].size() - pos));
        for (int c = 0; c < nch; ++c) {
            ip[c] = buf[c].data() + pos;
            op[c] = o[c].data();
        }
        d.process(ip.data(), op.data(), nch, m);
    }
    if (!d.detection().everLocked)
        return false;
    freq = d.detection().frequency;
    return true;
}
} // namespace

Renderer::~Renderer()
{
    cancel();
    join();
}

void Renderer::join()
{
    if (thread_.joinable())
        thread_.join();
}

std::string Renderer::stage() const
{
    std::lock_guard<std::mutex> l(mutex_);
    return stage_;
}

void Renderer::start(const AudioBuffer& audio, size_t start, size_t length, const ChainSettings& chain, double loHz,
                     double hiHz)
{
    cancel();
    join();
    cancel_.store(false);
    done_.store(false);
    progress_.store(0.0f);
    {
        std::lock_guard<std::mutex> l(mutex_);
        result_.clear();
        stage_ = "Preparing";
    }

    // Copy just the region plus context, so the document can move on.
    const size_t n = audio.numFrames();
    start = std::min(start, n);
    length = std::min(length, n - start);
    const size_t pre = std::min(start, static_cast<size_t>(kPreRollSeconds * audio.sampleRate));
    const size_t post = std::min(n - start - length, static_cast<size_t>(kPostRollSeconds * audio.sampleRate));
    AudioBuffer slice;
    slice.sampleRate = audio.sampleRate;
    slice.channels.resize(audio.numChannels());
    for (int c = 0; c < audio.numChannels(); ++c)
        slice.channels[c].assign(audio.channels[c].begin() + static_cast<long>(start - pre),
                                 audio.channels[c].begin() + static_cast<long>(start + length + post));

    std::string label;
    for (ToolId id : chainOrder())
        if (chain[static_cast<int>(id)].enabled)
            label += (label.empty() ? "" : " + ") + std::string(toolName(id));

    const bool band = hiHz > loHz && hiHz > 0.0;
    if (band)
        label += " (spectral)";
    running_.store(true);
    thread_ = std::thread([this, slice = std::move(slice), pre, start, length, chain, label, band, loHz, hiHz]() {
        auto out = renderRegion(slice, pre, length, chain, &cancel_, &progress_, &stage_, &mutex_);
        if (band && !out.empty() && !cancel_.load()) {
            {
                std::lock_guard<std::mutex> l(mutex_);
                stage_ = "Confining to the selected band";
            }
            std::vector<std::vector<float>> original(slice.channels.size());
            for (size_t c = 0; c < slice.channels.size(); ++c)
                original[c].assign(slice.channels[c].begin() + static_cast<long>(pre),
                                   slice.channels[c].begin() + static_cast<long>(pre + length));
            mend::offline::spectralBlend(out, original, slice.sampleRate, loHz, hiHz);
        }
        {
            std::lock_guard<std::mutex> l(mutex_);
            if (!cancel_.load() && !out.empty()) {
                result_ = std::move(out);
                resultStart_ = start;
                resultLabel_ = label;
                done_.store(true);
            }
        }
        progress_.store(1.0f);
        running_.store(false);
    });
}

bool Renderer::takeResult(size_t& start, std::vector<std::vector<float>>& out, std::string& label)
{
    if (running_.load() || !done_.load())
        return false;
    join();
    std::lock_guard<std::mutex> l(mutex_);
    start = resultStart_;
    out = std::move(result_);
    label = resultLabel_;
    result_.clear();
    done_.store(false);
    return !out.empty();
}

std::vector<std::vector<float>> Renderer::renderRegion(const AudioBuffer& audio, size_t start, size_t length,
                                                       const ChainSettings& chainIn, std::atomic<bool>* cancel,
                                                       std::atomic<float>* progress, std::string* stageOut,
                                                       std::mutex* stageMutex)
{
    auto setStage = [&](const std::string& s) {
        if (stageOut && stageMutex) {
            std::lock_guard<std::mutex> l(*stageMutex);
            *stageOut = s;
        }
    };
    const int nch = audio.numChannels();
    const double fs = audio.sampleRate;
    const size_t n = audio.numFrames();
    if (nch == 0 || length == 0 || start + length > n)
        return {};

    // Work buffer: [warm-up][region + following context]. With less real
    // audio before the region than we'd like, pad with a mirror image of
    // the region's opening (same sound, so detectors settle the same way).
    const size_t want = static_cast<size_t>(kPreRollSeconds * fs);
    const size_t realPre = start;
    const size_t mirror = realPre >= want ? 0 : std::min(want - realPre, length);
    Channels buf(nch);
    for (int c = 0; c < nch; ++c) {
        auto& b = buf[c];
        b.reserve(mirror + n);
        for (size_t i = 0; i < mirror; ++i)
            b.push_back(audio.channels[c][start + mirror - 1 - i]);
        b.insert(b.end(), audio.channels[c].begin(), audio.channels[c].end());
    }
    const size_t regionAt = mirror + start; // where the region starts in buf

    ChainSettings chain = chainIn;
    std::vector<ToolId> active;
    for (ToolId id : chainOrder())
        if (chain[static_cast<int>(id)].enabled)
            active.push_back(id);
    if (active.empty())
        return {};

    // De-hum auto: find the hum over the whole region first.
    auto& hum = chain[static_cast<int>(ToolId::DeHum)];
    if (hum.enabled && hum.values[0] > 0.5f) {
        setStage("Finding hum");
        Channels region(nch);
        for (int c = 0; c < nch; ++c)
            region[c].assign(buf[c].begin() + static_cast<long>(regionAt), buf[c].begin() + static_cast<long>(regionAt + length));
        double f = 0.0;
        if (detectHum(region, fs, f)) {
            hum.values[0] = 0.0f; // manual, at the detected frequency
            hum.values[1] = static_cast<float>(f);
        } else {
            hum.enabled = false; // no hum here: leave the audio alone
            active.erase(std::remove(active.begin(), active.end(), ToolId::DeHum), active.end());
        }
    }

    const float span = active.empty() ? 1.0f : 1.0f / static_cast<float>(active.size());
    for (size_t t = 0; t < active.size(); ++t) {
        const ToolId id = active[t];
        setStage(toolName(id));
        auto p = makeProcessor(id);
        p->prepare(fs, nch, kBlock);
        p->apply(chain[static_cast<int>(id)]);

        if (id == ToolId::DeHum) {
            // De-hum's narrow notches take time to settle (several times
            // Q / (pi f)). With too little real audio before the region,
            // run it both forwards and backwards: the backward pass is
            // settled at the region's start, the forward pass after it.
            const auto& hs = chain[static_cast<int>(ToolId::DeHum)];
            const double f0 = std::max(30.0, static_cast<double>(hs.values[1]));
            const double settle = std::clamp(5.0 * hs.values[3] / (kPi * f0), 0.3, 6.0);
            const size_t settleFrames = static_cast<size_t>(settle * fs);
            const size_t realStart = mirror; // first real sample in buf
            if (regionAt - realStart < settleFrames) {
                Channels fwd(buf.begin(), buf.end()), bwd(buf.begin(), buf.end());
                if (!runAligned(*p, fwd, cancel, progress, span * t, span * 0.5f))
                    return {};
                auto p2 = makeProcessor(id);
                p2->prepare(fs, nch, kBlock);
                p2->apply(hs);
                if (!runReversed(*p2, bwd, cancel, progress, span * (t + 0.5f), span * 0.5f))
                    return {};
                for (int c = 0; c < nch; ++c)
                    for (size_t i = 0; i < buf[c].size(); ++i) {
                        const double since = i >= realStart ? static_cast<double>(i - realStart) : 0.0;
                        const double u = std::clamp((since - 0.5 * settleFrames) / (0.5 * settleFrames), 0.0, 1.0);
                        const double w = u * u * (3.0 - 2.0 * u); // smoothstep
                        buf[c][i] = static_cast<float>(w * fwd[c][i] + (1.0 - w) * bwd[c][i]);
                    }
                continue;
            }
        }

        if (!runAligned(*p, buf, cancel, progress, span * t, span))
            return {};
    }

    Channels out(nch);
    for (int c = 0; c < nch; ++c)
        out[c].assign(buf[c].begin() + static_cast<long>(regionAt), buf[c].begin() + static_cast<long>(regionAt + length));

    // Crossfade into the untouched audio at the edges of a partial selection.
    const size_t fade = std::min(static_cast<size_t>(kEdgeFadeSeconds * fs), length / 4);
    for (int c = 0; c < nch; ++c) {
        if (start > 0)
            for (size_t i = 0; i < fade; ++i) {
                const float w = static_cast<float>(i + 1) / static_cast<float>(fade + 1);
                out[c][i] = audio.channels[c][start + i] * (1.0f - w) + out[c][i] * w;
            }
        if (start + length < n)
            for (size_t i = 0; i < fade; ++i) {
                const size_t k = length - 1 - i;
                const float w = static_cast<float>(i + 1) / static_cast<float>(fade + 1);
                out[c][k] = audio.channels[c][start + k] * (1.0f - w) + out[c][k] * w;
            }
    }
    setStage("Done");
    return out;
}

} // namespace mend::editor
