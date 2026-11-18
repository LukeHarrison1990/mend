// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
//
// Declarative parameter lists shared by each tool's DSP and UI halves.
#pragma once

#include <vector>

namespace mend {

enum ParamFlags : unsigned {
    kPBool = 1u << 0,
    kPInt = 1u << 1,
    kPLog = 1u << 2,
    kPOutput = 1u << 3, // DSP -> UI meter, not automatable
};

struct ParamSpec {
    const char* name;
    const char* symbol;
    const char* unit;
    float min, max, def;
    unsigned flags;
};

template <unsigned N>
std::vector<float> defaultsOf(const ParamSpec (&specs)[N])
{
    std::vector<float> d(N);
    for (unsigned i = 0; i < N; ++i)
        d[i] = specs[i].def;
    return d;
}

} // namespace mend

#ifdef DISTRHO_PLUGIN_HPP_INCLUDED
START_NAMESPACE_DISTRHO
// Fill a DPF Parameter from a spec (DSP side).
inline void initFromSpec(const mend::ParamSpec& s, Parameter& p)
{
    p.name = s.name;
    p.symbol = s.symbol;
    p.unit = s.unit;
    p.ranges.min = s.min;
    p.ranges.max = s.max;
    p.ranges.def = s.def;
    if (s.flags & mend::kPOutput) {
        p.hints = kParameterIsOutput;
        return;
    }
    p.hints = kParameterIsAutomatable;
    if (s.flags & mend::kPBool)
        p.hints |= kParameterIsBoolean | kParameterIsInteger;
    if (s.flags & mend::kPInt)
        p.hints |= kParameterIsInteger;
    if (s.flags & mend::kPLog)
        p.hints |= kParameterIsLogarithmic;
}
END_NAMESPACE_DISTRHO
#endif
