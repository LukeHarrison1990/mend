# SPDX-License-Identifier: MIT
# Mend Voice: standalone app, VST3, CLAP, LV2 and (on macOS) AU.
dpf_add_plugin(MendVoice
    TARGETS jack vst3 clap lv2 au
    UI_TYPE opengl
    USE_FILE_BROWSER # shared DGL build with the editor app
    MONOLITHIC # required by DIRECT_ACCESS for LV2
    FILES_DSP
        VoicePlugin.cpp
    FILES_UI
        VoiceUI.cpp
)
target_include_directories(MendVoice PUBLIC . ../common)
target_link_libraries(MendVoice PUBLIC mend-core mend-ui)
