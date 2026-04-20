cmake_minimum_required(VERSION 3.15)
project(DefineExtractor VERSION 1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

find_package(Threads REQUIRED)

# ── Main executable ────────────────────────────────────────────────────────
add_executable(DefineExtractor DefineExtractor/DefineExtractor.cpp)
target_link_libraries(DefineExtractor PRIVATE Threads::Threads)
if(MSVC)
    target_compile_definitions(DefineExtractor PRIVATE NOMINMAX)
    target_compile_options(DefineExtractor PRIVATE /utf-8)
endif()

# ── Tests (Catch2 v2 single-header) ───────────────────────────────────────
enable_testing()

add_executable(test_helpers tests/test_helpers.cpp)
target_include_directories(test_helpers PRIVATE
    ${CMAKE_SOURCE_DIR}           # for DefineExtractor/helpers.h
    ${CMAKE_SOURCE_DIR}/tests     # for catch2/catch.hpp
)
if(MSVC)
    target_compile_definitions(test_helpers PRIVATE NOMINMAX)
    target_compile_options(test_helpers PRIVATE /utf-8)
endif()

add_test(NAME HelperTests COMMAND test_helpers)
