// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
//
// miniaudio helpers shared by the editor's player and the standalone
// apps' stream.
#pragma once

#include "miniaudio.h"

#include <string>

namespace mend::audio {

// One audio context for the whole process (created on first use). Null if
// the system has no usable audio backend.
ma_context* sharedContext();

// Find a device by name. Empty name, or no match: returns false.
bool findDevice(ma_context* ctx, bool capture, const std::string& name, ma_device_id& id);

} // namespace mend::audio
