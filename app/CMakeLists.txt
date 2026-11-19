# SPDX-License-Identifier: MIT
# Mend Editor: the standalone app (open a file, see it, repair it).

find_package(Threads REQUIRED)
include("${PROJECT_SOURCE_DIR}/cmake/MendCodecs.cmake")

# Engine: everything except the window. Also used by the tests.
add_library(mend-editor-engine STATIC
    src/AudioFile.cpp
    src/Document.cpp
    src/MiniAudio.cpp
    src/Modules.cpp
    src/Project.cpp
    src/Player.cpp
    src/Renderer.cpp
    src/Spectrogram.cpp
    src/Tools.cpp
)
target_include_directories(mend-editor-engine PUBLIC src "${PROJECT_SOURCE_DIR}/external/miniaudio"
    PRIVATE "${PROJECT_SOURCE_DIR}/external/stb_vorbis")
target_link_libraries(mend-editor-engine PUBLIC mend-core mend-opusfile Threads::Threads ${CMAKE_DL_LIBS})
if(WIN32)
    target_link_libraries(mend-editor-engine PUBLIC mfuuid ole32) # mfplat/mfreadwrite are loaded at run time
endif()
set_source_files_properties(src/MiniAudio.cpp PROPERTIES COMPILE_OPTIONS "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-w>")
if(UNIX AND NOT APPLE)
    target_link_libraries(mend-editor-engine PUBLIC m)
endif()
if(APPLE)
    target_link_libraries(mend-editor-engine PUBLIC "-framework CoreFoundation" "-framework CoreAudio" "-framework AudioToolbox")
endif()

# The app window needs DGL from DPF.
if(MEND_BUILD_PLUGINS)
    add_executable(MendEditor WIN32 MACOSX_BUNDLE
        src/main.cpp
        src/EditorWindow.cpp
    )
    target_link_libraries(MendEditor PRIVATE mend-editor-engine mend-ui dgl-opengl dgl-opengl-definitions)
    target_include_directories(MendEditor PRIVATE "${PROJECT_SOURCE_DIR}/external/DPF/distrho")
    set_target_properties(MendEditor PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
    if(WIN32)
        target_sources(MendEditor PRIVATE ${MEND_RC}) # icon and version info
    elseif(APPLE)
        set(icns "${CMAKE_CURRENT_SOURCE_DIR}/resources/mend.icns")
        target_sources(MendEditor PRIVATE ${icns})
        set_source_files_properties(${icns} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
        set_target_properties(MendEditor PROPERTIES
            MACOSX_BUNDLE_BUNDLE_NAME "Mend Editor"
            MACOSX_BUNDLE_GUI_IDENTIFIER "audio.mend.editor"
            MACOSX_BUNDLE_ICON_FILE mend.icns
            MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
            MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
            MACOSX_BUNDLE_COPYRIGHT "MIT licence")
    else()
        include(GNUInstallDirs)
        install(TARGETS MendEditor RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
        install(FILES resources/mend.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
        foreach(size 16 24 32 48 64 128 256 512)
            install(FILES "${PROJECT_SOURCE_DIR}/assets/branding/png/mend-icon-${size}.png"
                    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps RENAME mend.png)
        endforeach()
        install(FILES "${PROJECT_SOURCE_DIR}/assets/branding/mend-icon.svg"
                DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps RENAME mend.svg)
    endif()
endif()
