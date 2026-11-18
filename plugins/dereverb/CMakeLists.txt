# SPDX-License-Identifier: MIT
# Mend De-reverb: standalone app, VST3, CLAP, LV2 and (on macOS) AU.
dpf_add_plugin(MendDeReverb
    TARGETS jack vst3 clap lv2 au
    UI_TYPE opengl
    USE_FILE_BROWSER # shared DGL build with the editor app
    MONOLITHIC # required by DIRECT_ACCESS for LV2
    FILES_DSP
        DeReverbPlugin.cpp
    FILES_UI
        DeReverbUI.cpp
)
target_include_directories(MendDeReverb PUBLIC . ../common)
target_link_libraries(MendDeReverb PUBLIC mend-core mend-ui)
