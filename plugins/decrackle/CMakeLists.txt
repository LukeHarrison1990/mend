# SPDX-License-Identifier: MIT
# Mend De-crackle: standalone app, VST3, CLAP, LV2 and (on macOS) AU.
dpf_add_plugin(MendDeCrackle
    TARGETS jack vst3 clap lv2 au
    UI_TYPE opengl
    USE_FILE_BROWSER # shared DGL build with the editor app
    MONOLITHIC # required by DIRECT_ACCESS for LV2
    FILES_DSP
        DeCracklePlugin.cpp
    FILES_UI
        DeCrackleUI.cpp
)
target_include_directories(MendDeCrackle PUBLIC . ../common)
target_link_libraries(MendDeCrackle PUBLIC mend-core mend-ui)
