# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.15)
project(Mend VERSION 0.8.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(MEND_BUILD_PLUGINS "Build the plugins and standalone apps (needs DPF in external/DPF)" ON)
option(MEND_BUILD_TESTS "Build the DSP unit tests" ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

add_subdirectory(core)


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
        if(WIN32 AND TARGET ${target}-jack)
            target_sources(${target}-jack PRIVATE ${MEND_RC})
        endif()
    endforeach()
endif()

add_subdirectory(app)

if(MEND_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
