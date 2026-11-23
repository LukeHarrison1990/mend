# SPDX-License-Identifier: MIT
# mend-core: the DSP for every tool in the suite. Plain C++17, no framework
# dependencies, so it can be shared by the plugins, the standalone editor
# and command-line tools alike.

add_library(mend-core STATIC
    src/ArModel.cpp
    src/BlockRepairer.cpp
    src/DeClick.cpp
    src/DeClip.cpp
    src/DeNoiseEngine.cpp
    src/DeepFilter.cpp
    src/DeepFilterNet3Weights.cpp
    src/DeHum.cpp
    src/DePlosive.cpp
    src/Dynamics.cpp
    src/Eq.cpp
    src/Fft.cpp
    src/HumDetector.cpp
    src/NeuralDenoise.cpp
    src/Offline.cpp
    src/SpectralDenoise.cpp
    src/SpectralTools.cpp
    src/Stft.cpp
    src/VoiceClean.cpp
)

target_include_directories(mend-core PUBLIC include)
target_compile_features(mend-core PUBLIC cxx_std_17)
set_target_properties(mend-core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    target_compile_options(mend-core PRIVATE /W4)
else()
    target_compile_options(mend-core PRIVATE -Wall -Wextra)
endif()

# The neural network's inner loops need the compiler free to reorder
# floating-point sums (vectorised), which is several times faster.
if(MSVC)
    set_source_files_properties(src/DeepFilter.cpp PROPERTIES COMPILE_OPTIONS "/fp:fast;/O2")
else()
    set_source_files_properties(src/DeepFilter.cpp PROPERTIES COMPILE_OPTIONS "-O3;-ffast-math")
endif()
