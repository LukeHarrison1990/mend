# SPDX-License-Identifier: MIT
# Mend De-wind: standalone app, VST3, CLAP, LV2 and (on macOS) AU.
dpf_add_plugin(MendDeWind
    TARGETS jack vst3 clap lv2 au
    UI_TYPE opengl
    USE_FILE_BROWSER # shared DGL build with the editor app
    MONOLITHIC # required by DIRECT_ACCESS for LV2
    FILES_DSP
        DeWindPlugin.cpp
    FILES_UI
        DeWindUI.cpp
)
target_include_directories(MendDeWind PUBLIC . ../common)
target_link_libraries(MendDeWind PUBLIC mend-core mend-ui)
