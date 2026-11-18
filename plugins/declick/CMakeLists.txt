# SPDX-License-Identifier: MIT
dpf_add_plugin(MendDeClick
    TARGETS jack vst3 clap lv2 au
    UI_TYPE opengl
    USE_FILE_BROWSER # shared DGL build with the editor app
    MONOLITHIC # required by DIRECT_ACCESS for LV2
    FILES_DSP
        DeClickPlugin.cpp
    FILES_UI
        DeClickUI.cpp
)
target_include_directories(MendDeClick PUBLIC . ../common)
target_link_libraries(MendDeClick PUBLIC mend-core mend-ui)
