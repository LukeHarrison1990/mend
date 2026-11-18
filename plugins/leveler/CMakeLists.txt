# SPDX-License-Identifier: MIT
# Mend Leveler: standalone app, VST3, CLAP, LV2 and (on macOS) AU.
dpf_add_plugin(MendLeveler
    TARGETS jack vst3 clap lv2 au
    UI_TYPE opengl
    USE_FILE_BROWSER # shared DGL build with the editor app
    MONOLITHIC # required by DIRECT_ACCESS for LV2
    FILES_DSP
        LevelerPlugin.cpp
    FILES_UI
        LevelerUI.cpp
)
target_include_directories(MendLeveler PUBLIC . ../common)
target_link_libraries(MendLeveler PUBLIC mend-core mend-ui)
