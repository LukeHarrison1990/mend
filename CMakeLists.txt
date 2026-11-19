# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.15)
project(Mend VERSION 0.9.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(MEND_BUILD_PLUGINS "Build the plugins and standalone apps (needs DPF in external/DPF)" ON)
option(MEND_BUILD_TESTS "Build the DSP unit tests" ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

add_subdirectory(core)
add_subdirectory(audio)


if(MEND_BUILD_PLUGINS)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/DPF/CMakeLists.txt")
        message(FATAL_ERROR
            "DPF not found in external/DPF (it is vendored in the repository).\n"
            "Configure with -DMEND_BUILD_PLUGINS=OFF to build only the core.")
    endif()
    add_subdirectory(external/DPF)
    add_subdirectory(ui)
    # folder -> plugin target name
    set(MEND_PLUGINS
        dehum:MendDeHum declick:MendDeClick declip:MendDeClip deplosive:MendDePlosive denoise:MendDeNoise
        decrackle:MendDeCrackle deess:MendDeEss breathcontrol:MendBreathControl dewind:MendDeWind
        dereverb:MendDeReverb centerextract:MendCentreExtract eq:MendEq leveler:MendLeveler voice:MendVoice)
    if(WIN32)
        # icon and version info for every .exe
        set(MEND_ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/app/resources/mend.ico")
        configure_file(app/resources/mend.rc.in "${CMAKE_CURRENT_BINARY_DIR}/mend.rc" @ONLY)
        set(MEND_RC "${CMAKE_CURRENT_BINARY_DIR}/mend.rc")
    endif()
    foreach(entry ${MEND_PLUGINS})
        string(REPLACE ":" ";" parts ${entry})
        list(GET parts 0 folder)
        list(GET parts 1 target)
        add_subdirectory(plugins/${folder})
        if(TARGET ${target}-jack)
            # standalone app: audio through mend-audio (device choice) when
            # JACK isn't running; see external/DPF/.../MendAudioBridge.hpp
            target_link_libraries(${target}-jack PRIVATE mend-audio)
            target_compile_definitions(${target}-jack PRIVATE MEND_NATIVE_AUDIO)
            if(WIN32)
                target_sources(${target}-jack PRIVATE ${MEND_RC})
            elseif(APPLE)
                # a proper app bundle ("Mend Voice.app"), with the microphone
                # usage text macOS needs before it allows audio input
                file(STRINGS plugins/${folder}/DistrhoPluginInfo.h name_line REGEX "#define DISTRHO_PLUGIN_NAME ")
                string(REGEX REPLACE ".*\"(.*)\".*" "\\1" display_name "${name_line}")
                set(icns "${CMAKE_CURRENT_SOURCE_DIR}/app/resources/mend.icns")
                target_sources(${target}-jack PRIVATE ${icns})
                set_source_files_properties(${icns} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
                set_target_properties(${target}-jack PROPERTIES
                    MACOSX_BUNDLE TRUE
                    OUTPUT_NAME "${display_name}"
                    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/app/resources/standalone-Info.plist.in"
                    MACOSX_BUNDLE_BUNDLE_NAME "${display_name}"
                    MACOSX_BUNDLE_GUI_IDENTIFIER "audio.mend.${folder}.app"
                    MACOSX_BUNDLE_ICON_FILE mend.icns
                    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
                    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
                    MACOSX_BUNDLE_COPYRIGHT "MIT licence")
            endif()
        endif()
    endforeach()
endif()

add_subdirectory(app)

if(MEND_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
