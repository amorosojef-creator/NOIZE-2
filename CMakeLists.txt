cmake_minimum_required(VERSION 3.22)
project(NOIZE VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Windows: link the C++ runtime statically so users need no Visual C++ redistributable.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# JUCE 9.0.5 (AGPLv3). Use third_party/JUCE if present, otherwise download that exact tag.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/JUCE/CMakeLists.txt")
    add_subdirectory(third_party/JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 9.0.5 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

option(NOIZE_BUILD_TESTS "Build headless engine tests" ON)
option(NOIZE_BUILD_APP   "Build the NOIZE desktop application" ON)

# ---------------------------------------------------------------------------
# noize_core: audio engine, instruments, effects, project model (no GUI)
# ---------------------------------------------------------------------------
add_library(noize_core STATIC
    Source/Core/Project.cpp
    Source/Core/Effects.cpp
    Source/Core/Instruments.cpp
    Source/Core/Engine.cpp
    Source/Core/Exporter.cpp
)
target_include_directories(noize_core PUBLIC Source)
target_link_libraries(noize_core PUBLIC
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_core
    juce::juce_data_structures
    juce::juce_events
  PRIVATE
    juce::juce_recommended_config_flags
)
target_compile_definitions(noize_core PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

# ---------------------------------------------------------------------------
# Headless tests
# ---------------------------------------------------------------------------
if(NOIZE_BUILD_TESTS)
    juce_add_console_app(noize_tests PRODUCT_NAME "noize_tests")
    target_sources(noize_tests PRIVATE Source/Tests/Tests.cpp)
    target_link_libraries(noize_tests PRIVATE noize_core juce::juce_recommended_config_flags)
    target_compile_definitions(noize_tests PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
    enable_testing()
    add_test(NAME noize_engine COMMAND noize_tests)
endif()

# ---------------------------------------------------------------------------
# Desktop application
# ---------------------------------------------------------------------------
if(NOIZE_BUILD_APP)
    juce_add_gui_app(NOIZE
        PRODUCT_NAME "NOIZE"
        COMPANY_NAME "NOIZE"
        VERSION ${PROJECT_VERSION})
    target_sources(NOIZE PRIVATE
        Source/Main.cpp
        Source/UI/MainWindow.cpp
    )
    target_link_libraries(NOIZE PRIVATE
        noize_core
        juce::juce_gui_extra
        juce::juce_opengl
        juce::juce_recommended_config_flags)
    target_compile_definitions(NOIZE PRIVATE
        JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 JUCE_APPLICATION_NAME_STRING="NOIZE"
        JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}")
endif()

# ---------------------------------------------------------------------------
# Install layout (used for the portable ZIP and the installer):  cmake --install <build> --component app --prefix dist/NOIZE
# ---------------------------------------------------------------------------
if(NOIZE_BUILD_APP)
    install(TARGETS NOIZE RUNTIME DESTINATION . COMPONENT app)
    install(DIRECTORY examples/ DESTINATION examples COMPONENT app)
    install(DIRECTORY docs/ DESTINATION docs COMPONENT app)
    install(FILES README.md LICENSE-NOTICE.md DESTINATION . COMPONENT app)
endif()
