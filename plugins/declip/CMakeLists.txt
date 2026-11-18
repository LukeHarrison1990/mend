# SPDX-License-Identifier: MIT
dpf_add_plugin(MendDeClip
    TARGETS jack vst3 clap lv2 au
    UI_TYPE opengl
    USE_FILE_BROWSER # shared DGL build with the editor app
    MONOLITHIC # required by DIRECT_ACCESS for LV2
    FILES_DSP
        DeClipPlugin.cpp
    FILES_UI
        DeClipUI.cpp
)
target_include_directories(MendDeClip PUBLIC . ../common)
target_link_libraries(MendDeClip PUBLIC mend-core mend-ui)
