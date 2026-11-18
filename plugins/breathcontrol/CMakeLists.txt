# SPDX-License-Identifier: MIT
# Mend Breath Control: standalone app, VST3, CLAP, LV2 and (on macOS) AU.
dpf_add_plugin(MendBreathControl
    TARGETS jack vst3 clap lv2 au
    UI_TYPE opengl
    USE_FILE_BROWSER # shared DGL build with the editor app
    MONOLITHIC # required by DIRECT_ACCESS for LV2
    FILES_DSP
        BreathControlPlugin.cpp
    FILES_UI
        BreathControlUI.cpp
)
target_include_directories(MendBreathControl PUBLIC . ../common)
target_link_libraries(MendBreathControl PUBLIC mend-core mend-ui)
