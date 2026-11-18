// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#pragma once

#include <vector>

namespace mend {

// Base for repair tools that need context on both sides of a defect
// (De-click, De-clip). Audio is gathered into fixed blocks of B samples.
// Each block is repaired while the previous (already repaired) block and
// the next (raw, look-ahead) block are available as context:
//
//     x[0, B)    previous block, already repaired and output
//     x[B, 2B)   current block: repair this
//     x[2B, 3B)  look-ahead block (raw; repairs may extend into it)
//
// Latency is a constant 2B samples, whatever the host block size.
class BlockRepairer {
public:
    virtual ~BlockRepairer() = default;

    int latency() const noexcept { return 2 * block_; }
    int blockSize() const noexcept { return block_; }

    void process(const float* const* in, float* const* out, int numChannels, int numSamples);

protected:
    void prepareBlocks(int blockSize, int numChannels);
    void resetBlocks();

    // Called once per block before the channels are repaired.
    virtual void beginBlock() {}
    // Repair x[B, 2B) in place (see layout above).
    virtual void repairBlock(int channel, double* x) = 0;
    // Write the output for the current block. Default: the repaired audio.
    virtual void renderBlock(int channel, const double* repaired, const double* original, float* dst);
    // Called once per block after all channels.
    virtual void endBlock() {}

    // Raw, unmodified audio in the same 3B layout.
    const double* raw(int channel) const noexcept { return ch_[channel].raw.data(); }
    int numChannels() const noexcept { return numChannels_; }

private:
    void runBlock();

    struct Channel {
        std::vector<double> work, raw;
        std::vector<float> outQueue;
    };
    std::vector<Channel> ch_;
    std::vector<float> rendered_;
    int block_ = 0, numChannels_ = 0, fill_ = 0;
    int qRead_ = 0, qWrite_ = 0, qCount_ = 0, qCap_ = 0;
};

} // namespace mend
