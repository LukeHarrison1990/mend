# SPDX-License-Identifier: MIT
# Mend EQ: standalone app, VST3, CLAP, LV2 and (on macOS) AU.
dpf_add_plugin(MendEq
    TARGETS jack vst3 clap lv2 au
    UI_TYPE opengl
    USE_FILE_BROWSER # shared DGL build with the editor app
    MONOLITHIC # required by DIRECT_ACCESS for LV2
    FILES_DSP
        EqPlugin.cpp
    FILES_UI
        EqUI.cpp
)
target_include_directories(MendEq PUBLIC . ../common)
target_link_libraries(MendEq PUBLIC mend-core mend-ui)
