// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#include "mend/BlockRepairer.hpp"

#include <algorithm>

namespace mend {

void BlockRepairer::prepareBlocks(int blockSize, int numChannels)
{
    block_ = blockSize;
    numChannels_ = numChannels;
    ch_.assign(numChannels, Channel{});
    qCap_ = 4 * blockSize;
    for (auto& c : ch_) {
        c.work.assign(3 * blockSize, 0.0);
        c.raw.assign(3 * blockSize, 0.0);
        c.outQueue.assign(qCap_, 0.0f);
    }
    rendered_.assign(blockSize, 0.0f);
    resetBlocks();
}

void BlockRepairer::resetBlocks()
{
    for (auto& c : ch_) {
        std::fill(c.work.begin(), c.work.end(), 0.0);
        std::fill(c.raw.begin(), c.raw.end(), 0.0);
        std::fill(c.outQueue.begin(), c.outQueue.end(), 0.0f);
    }
    fill_ = 0;
    // Pre-fill one block of silence; together with the one-block look-ahead
    // this gives a constant 2B latency.
    qRead_ = 0;
    qWrite_ = block_ % qCap_;
    qCount_ = block_;
}

void BlockRepairer::renderBlock(int, const double* repaired, const double*, float* dst)
{
    for (int i = 0; i < block_; ++i)
        dst[i] = static_cast<float>(repaired[i]);
}

void BlockRepairer::runBlock()
{
    const int B = block_;
    beginBlock();
    for (int c = 0; c < numChannels_; ++c) {
        Channel& ch = ch_[c];
        repairBlock(c, ch.work.data());
        renderBlock(c, ch.work.data() + B, ch.raw.data() + B, rendered_.data());
        for (int i = 0; i < B; ++i)
            ch.outQueue[(qWrite_ + i) % qCap_] = rendered_[i];
        // shift: current -> previous, look-ahead -> current
        std::copy(ch.work.begin() + B, ch.work.end(), ch.work.begin());
        std::copy(ch.raw.begin() + B, ch.raw.end(), ch.raw.begin());
    }
    endBlock();
    qWrite_ = (qWrite_ + B) % qCap_;
    qCount_ += B;
}

void BlockRepairer::process(const float* const* in, float* const* out, int numChannels, int numSamples)
{
    numChannels = std::min(numChannels, numChannels_);
    const int B = block_;
    for (int s = 0; s < numSamples; ++s) {
        for (int c = 0; c < numChannels; ++c) {
            ch_[c].work[2 * B + fill_] = in[c][s];
            ch_[c].raw[2 * B + fill_] = in[c][s];
        }
        for (int c = numChannels; c < numChannels_; ++c) {
            ch_[c].work[2 * B + fill_] = 0.0;
            ch_[c].raw[2 * B + fill_] = 0.0;
        }
        if (++fill_ == B) {
            fill_ = 0;
            runBlock();
        }
        for (int c = 0; c < numChannels; ++c)
            out[c][s] = ch_[c].outQueue[qRead_];
        qRead_ = (qRead_ + 1) % qCap_;
        --qCount_;
    }
}

} // namespace mend
