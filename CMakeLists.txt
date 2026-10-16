cmake_minimum_required(VERSION 3.15)

project(VoiceFramePro VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/juce/CMakeLists.txt")
    add_subdirectory(juce)
else()
    include(FetchContent)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 7.0.12
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(JUCE)
endif()

# RNNoise (vendored, BSD-3-Clause) - see third_party/rnnoise/README.vendored
add_library(rnnoise STATIC
    third_party/rnnoise/src/celt_lpc.c
    third_party/rnnoise/src/denoise.c
    third_party/rnnoise/src/kiss_fft.c
    third_party/rnnoise/src/pitch.c
    third_party/rnnoise/src/rnn.c
    third_party/rnnoise/src/rnn_data.c
    third_party/rnnoise/src/rnn_reader.c
)
target_include_directories(rnnoise
    PUBLIC third_party/rnnoise/include
    PRIVATE third_party/rnnoise/src
)
target_compile_definitions(rnnoise PRIVATE _CRT_SECURE_NO_WARNINGS)
set_target_properties(rnnoise PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(rnnoise PRIVATE /W0)
else()
    target_compile_options(rnnoise PRIVATE -w)
endif()

# Embedded Geist / Geist Mono typefaces (SIL OFL 1.1) - see Resources/fonts/OFL.txt
juce_add_binary_data(VoiceFrameProBinaryData
    SOURCES
        Resources/fonts/Geist-Regular.ttf
        Resources/fonts/Geist-Medium.ttf
        Resources/fonts/Geist-SemiBold.ttf
        Resources/fonts/Geist-Bold.ttf
        Resources/fonts/GeistMono-Regular.ttf
        Resources/fonts/GeistMono-Medium.ttf
        Resources/fonts/GeistMono-SemiBold.ttf
)

juce_add_plugin(VoiceFramePro
    COMPANY_NAME "VoiceFrame Audio"
    PLUGIN_MANUFACTURER_CODE Vfau
    PLUGIN_CODE Vfpr
    FORMATS VST3
    PRODUCT_NAME "VoiceFrame Pro"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
)

target_sources(VoiceFramePro
    PRIVATE
        Source/PluginProcessor.h
        Source/PluginProcessor.cpp
        Source/PluginEditor.h
        Source/PluginEditor.cpp
        Source/DenoiseEngine.h
        Source/DenoiseEngine.cpp
        Source/PresetManager.h
        Source/PresetManager.cpp
        Source/Theme.h
        Source/Theme.cpp
        Source/Widgets.h
        Source/Widgets.cpp
        Source/Displays.h
        Source/Displays.cpp
        Source/Views.h
        Source/Views.cpp
)

target_compile_definitions(VoiceFramePro
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        # JUCE 7.0.12 SIMDRegister does not compile with recent Apple Clang on arm64; nothing here needs it
        $<$<PLATFORM_ID:Darwin>:JUCE_USE_SIMD=0>
)

target_link_libraries(VoiceFramePro
    PRIVATE
        rnnoise
        VoiceFrameProBinaryData
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_extra
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)


# Test runner: DSP / state checks and editor snapshots (not part of the plugin)
option(VFP_BUILD_TESTS "Build the VoiceFrame Pro test runner" ON)
if(VFP_BUILD_TESTS)
    juce_add_console_app(VoiceFrameProTests PRODUCT_NAME "VoiceFrameProTests")

    target_sources(VoiceFrameProTests
        PRIVATE
            tests/VoiceFrameProTests.cpp
            Source/PluginProcessor.cpp
            Source/PluginEditor.cpp
            Source/DenoiseEngine.cpp
            Source/PresetManager.cpp
            Source/Theme.cpp
            Source/Widgets.cpp
            Source/Displays.cpp
            Source/Views.cpp
    )

    target_compile_definitions(VoiceFrameProTests
        PRIVATE
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_MODAL_LOOPS_PERMITTED=1
            JUCE_DISPLAY_SPLASH_SCREEN=0
            $<$<PLATFORM_ID:Darwin>:JUCE_USE_SIMD=0>
            JucePlugin_Name="VoiceFrame Pro"
            JucePlugin_VersionString="${PROJECT_VERSION}"
    )

    target_link_libraries(VoiceFrameProTests
        PRIVATE
            rnnoise
            VoiceFrameProBinaryData
            juce::juce_audio_processors
            juce::juce_audio_utils
            juce::juce_dsp
            juce::juce_gui_extra
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
    )
endif()
