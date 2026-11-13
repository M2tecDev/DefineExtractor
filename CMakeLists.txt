cmake_minimum_required(VERSION 3.25)
project(DefineExtractor VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Fetch dependencies via FetchContent ────────────────────────────────────
include(FetchContent)

FetchContent_Declare(
    Slint
    SYSTEM
    GIT_REPOSITORY https://github.com/slint-ui/slint.git
    GIT_TAG        e9c1ca295f9356af71f1e251c287de18406b46f6 # v1.16.1
    SOURCE_SUBDIR  api/cpp
)

FetchContent_Declare(
    Catch2
    SYSTEM
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.12.0
)

FetchContent_MakeAvailable(Slint Catch2)

find_package(Threads REQUIRED)

# ── Core library (header-only, shared between console and GUI) ─────────────
add_library(DefineExtractorCore INTERFACE)
target_include_directories(DefineExtractorCore INTERFACE ${CMAKE_SOURCE_DIR})
target_link_libraries(DefineExtractorCore INTERFACE Threads::Threads)

# ── Console executable ─────────────────────────────────────────────────────
add_executable(DefineExtractor DefineExtractor/DefineExtractor.cpp)
target_link_libraries(DefineExtractor PRIVATE DefineExtractorCore)
if(MSVC)
    target_compile_definitions(DefineExtractor PRIVATE NOMINMAX)
    target_compile_options(DefineExtractor PRIVATE /utf-8 /W4 /permissive-)
endif()

# ── GUI executable ─────────────────────────────────────────────────────────
# IMPORTANT: Only compile main_window.ui - other .ui files are imported and auto-generated
add_executable(DefineExtractorGUI
    DefineExtractor/gui/main.cpp
)
slint_target_sources(DefineExtractorGUI ${CMAKE_CURRENT_SOURCE_DIR}/DefineExtractor/gui/main_window.ui)
target_link_libraries(DefineExtractorGUI PRIVATE DefineExtractorCore Slint::Slint)
if(MSVC)
    target_compile_definitions(DefineExtractorGUI PRIVATE NOMINMAX)
    target_compile_options(DefineExtractorGUI PRIVATE /utf-8 /W4 /permissive-)
endif()

# Ensure dependent runtime DLLs (such as slint_cpp.dll) are next to the executable.
if(WIN32)
    add_custom_command(TARGET DefineExtractorGUI POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:DefineExtractorGUI>
            $<TARGET_FILE_DIR:DefineExtractorGUI>
        COMMAND_EXPAND_LISTS
    )

    install(TARGETS DefineExtractor DefineExtractorGUI
        RUNTIME DESTINATION . COMPONENT DefineExtractorPortable CONFIGURATIONS Release)
    install(FILES $<TARGET_RUNTIME_DLLS:DefineExtractorGUI>
        DESTINATION . COMPONENT DefineExtractorPortable CONFIGURATIONS Release)
    install(FILES "${slint_SOURCE_DIR}/LICENSE.md"
        DESTINATION licenses/slint COMPONENT DefineExtractorPortable CONFIGURATIONS Release)
    install(DIRECTORY "${slint_SOURCE_DIR}/LICENSES/"
        DESTINATION licenses/slint/LICENSES COMPONENT DefineExtractorPortable CONFIGURATIONS Release)

    if(MSVC)
        set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
        include(InstallRequiredSystemLibraries)
        install(FILES ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS}
            DESTINATION . COMPONENT DefineExtractorPortable CONFIGURATIONS Release)
    endif()
endif()

# ── Tests (Catch2 v3.x) ────────────────────────────────────────────────────
enable_testing()

add_executable(test_helpers tests/test_helpers.cpp tests/test_extraction.cpp)
target_link_libraries(test_helpers PRIVATE Catch2::Catch2WithMain DefineExtractorCore)
if(MSVC)
    target_compile_definitions(test_helpers PRIVATE NOMINMAX)
    target_compile_options(test_helpers PRIVATE /utf-8 /W4 /permissive-)
endif()

# Copy Catch2 DLLs next to the test executable on Windows
if(WIN32)
    add_custom_command(TARGET test_helpers POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:test_helpers>
            $<TARGET_FILE_DIR:test_helpers>
        COMMAND_EXPAND_LISTS
    )
endif()

add_test(NAME HelperTests COMMAND test_helpers)
