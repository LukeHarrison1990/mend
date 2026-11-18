# SPDX-License-Identifier: MIT
# Mend De-hum: standalone app (jack target with native audio fallback),
# VST3, CLAP, LV2 and, on macOS, AU.

dpf_add_plugin(MendDeHum
    TARGETS jack vst3 clap lv2 au
    UI_TYPE opengl
    USE_FILE_BROWSER # shared DGL build with the editor app
    MONOLITHIC # required by DIRECT_ACCESS for LV2
    FILES_DSP
        DeHumPlugin.cpp
    FILES_UI
        DeHumUI.cpp
)

target_include_directories(MendDeHum PUBLIC .)
target_link_libraries(MendDeHum PUBLIC mend-core mend-ui)
