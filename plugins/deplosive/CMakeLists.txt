# SPDX-License-Identifier: MIT
dpf_add_plugin(MendDePlosive
    TARGETS jack vst3 clap lv2 au
    UI_TYPE opengl
    USE_FILE_BROWSER # shared DGL build with the editor app
    MONOLITHIC # required by DIRECT_ACCESS for LV2
    FILES_DSP
        DePlosivePlugin.cpp
    FILES_UI
        DePlosiveUI.cpp
)
target_include_directories(MendDePlosive PUBLIC . ../common)
target_link_libraries(MendDePlosive PUBLIC mend-core mend-ui)
