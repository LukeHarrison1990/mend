# SPDX-License-Identifier: MIT
# Mend De-ess: standalone app, VST3, CLAP, LV2 and (on macOS) AU.
dpf_add_plugin(MendDeEss
    TARGETS jack vst3 clap lv2 au
    UI_TYPE opengl
    USE_FILE_BROWSER # shared DGL build with the editor app
    MONOLITHIC # required by DIRECT_ACCESS for LV2
    FILES_DSP
        DeEssPlugin.cpp
    FILES_UI
        DeEssUI.cpp
)
target_include_directories(MendDeEss PUBLIC . ../common)
target_link_libraries(MendDeEss PUBLIC mend-core mend-ui)
