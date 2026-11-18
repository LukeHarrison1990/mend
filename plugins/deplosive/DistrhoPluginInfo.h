// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#pragma once

#define DISTRHO_PLUGIN_BRAND   "Mend"
#define DISTRHO_PLUGIN_NAME    "Mend De-plosive"
#define DISTRHO_PLUGIN_URI     "https://github.com/LukeHarrison1990/mend/deplosive"
#define DISTRHO_PLUGIN_CLAP_ID "audio.mend.deplosive"

#define DISTRHO_PLUGIN_BRAND_ID  Mend
#define DISTRHO_PLUGIN_UNIQUE_ID mDpl

#define DISTRHO_PLUGIN_HAS_UI        1
#define DISTRHO_PLUGIN_IS_RT_SAFE    1
#define DISTRHO_PLUGIN_NUM_INPUTS    2
#define DISTRHO_PLUGIN_NUM_OUTPUTS   2
#define DISTRHO_PLUGIN_WANT_LATENCY  1
#define DISTRHO_PLUGIN_WANT_PROGRAMS 0
#define DISTRHO_PLUGIN_WANT_STATE    0
#define DISTRHO_UI_USE_NANOVG        1
// Single-object VST3 (controller and processor share one instance), so
// parameter edits made by the host act on the DSP directly. Hosts such as
// Audacity 4 otherwise lose some generic-UI edits.
#define DISTRHO_PLUGIN_WANT_DIRECT_ACCESS 1
#define DISTRHO_UI_USER_RESIZABLE    1
#define DISTRHO_UI_DEFAULT_WIDTH     780
#define DISTRHO_UI_DEFAULT_HEIGHT    440

#define DISTRHO_PLUGIN_LV2_CATEGORY    "lv2:DynamicsPlugin"
#define DISTRHO_PLUGIN_VST3_CATEGORIES "Fx|Restoration|Stereo"
#define DISTRHO_PLUGIN_CLAP_FEATURES   "audio-effect", "restoration", "stereo"
