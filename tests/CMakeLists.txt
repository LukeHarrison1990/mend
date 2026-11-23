# SPDX-License-Identifier: MIT
foreach(t dehum declick declip deplosive denoise stft newtools offline voice neural)
    add_executable(test_${t} test_${t}.cpp)
    target_link_libraries(test_${t} PRIVATE mend-core)
    add_test(NAME ${t} COMMAND test_${t})
endforeach()

target_compile_definitions(test_neural PRIVATE MEND_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")

add_executable(test_editor test_editor.cpp)
target_link_libraries(test_editor PRIVATE mend-editor-engine)
add_test(NAME editor COMMAND test_editor)
