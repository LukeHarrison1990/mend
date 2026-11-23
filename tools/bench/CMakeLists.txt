# SPDX-License-Identifier: MIT
# Command-line De-noise, used by the benchmark scripts in this folder.
add_executable(mend-denoise mend_denoise.cpp)
target_link_libraries(mend-denoise PRIVATE mend-core)
