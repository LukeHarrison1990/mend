# SPDX-License-Identifier: MIT
# Bundled decoders for the editor: libogg + libopus + opusfile (BSD-3-Clause)
# for Opus, and stb_vorbis (public domain / MIT) for Ogg Vorbis via miniaudio.

set(_ext "${PROJECT_SOURCE_DIR}/external")

add_library(mend-ogg STATIC "${_ext}/ogg/src/bitwise.c" "${_ext}/ogg/src/framing.c")
target_include_directories(mend-ogg PUBLIC "${_ext}/ogg/include")
set_target_properties(mend-ogg PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(OPUS_DRED OFF CACHE BOOL "" FORCE)
set(OPUS_OSCE OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(_mend_saved_bt ${CMAKE_BUILD_TYPE})
add_subdirectory("${_ext}/opus" "${PROJECT_BINARY_DIR}/external/opus" EXCLUDE_FROM_ALL)
set_target_properties(opus PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(mend-opusfile STATIC
    "${_ext}/opusfile/src/info.c"
    "${_ext}/opusfile/src/internal.c"
    "${_ext}/opusfile/src/opusfile.c"
    "${_ext}/opusfile/src/stream.c")
target_include_directories(mend-opusfile PUBLIC "${_ext}/opusfile/include" PRIVATE "${_ext}/opusfile/src")
target_compile_definitions(mend-opusfile PRIVATE OP_DISABLE_HTTP OP_HAVE_LRINTF)
target_link_libraries(mend-opusfile PUBLIC mend-ogg opus)
set_target_properties(mend-opusfile PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(NOT MSVC)
    target_compile_options(mend-opusfile PRIVATE -w)
    target_compile_options(mend-ogg PRIVATE -w)
endif()
