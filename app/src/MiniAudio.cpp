// SPDX-License-Identifier: MIT
// miniaudio (public domain / MIT-0) implementation unit, with stb_vorbis
// (public domain / MIT) so miniaudio can decode Ogg Vorbis.
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENGINE
#define MA_NO_NODE_GRAPH
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_GENERATION
#include "miniaudio.h"

#undef STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"
