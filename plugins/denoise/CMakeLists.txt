# SPDX-License-Identifier: MIT
dpf_add_plugin(MendDeNoise
    TARGETS jack vst3 clap lv2 au
    UI_TYPE opengl
    USE_FILE_BROWSER # shared DGL build with the editor app
    MONOLITHIC # the UI reads the live spectrum straight from the DSP
    FILES_DSP
        DeNoisePlugin.cpp
    FILES_UI
        DeNoiseUI.cpp
)
target_include_directories(MendDeNoise PUBLIC . ../common)
target_link_libraries(MendDeNoise PUBLIC mend-core mend-ui)
