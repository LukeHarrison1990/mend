# SPDX-License-Identifier: MIT
# mend-audio: miniaudio, the saved audio settings and device choice, shared
# by the editor and the standalone apps.

find_package(Threads REQUIRED)

add_library(mend-audio STATIC
    src/AudioDevices.cpp
    src/MiniAudio.cpp
)
target_include_directories(mend-audio
    PUBLIC include "${PROJECT_SOURCE_DIR}/external/miniaudio"
    PRIVATE "${PROJECT_SOURCE_DIR}/external/stb_vorbis")
target_link_libraries(mend-audio PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
set_target_properties(mend-audio PROPERTIES POSITION_INDEPENDENT_CODE ON)
set_source_files_properties(src/MiniAudio.cpp PROPERTIES COMPILE_OPTIONS "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-w>")
if(WIN32)
    target_link_libraries(mend-audio PUBLIC ole32)
elseif(APPLE)
    target_link_libraries(mend-audio PUBLIC "-framework CoreFoundation" "-framework CoreAudio" "-framework AudioToolbox")
else()
    target_link_libraries(mend-audio PUBLIC m)
endif()
