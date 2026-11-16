# Opt-in libFuzzer target for the wire decoder (ENC-1). Clang only, never part of the default build.
cmake_minimum_required(VERSION 3.14...3.22)

project(MosaicFuzz LANGUAGES CXX)

set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(../cmake/CPM.cmake)
CPMAddPackage(NAME Mosaic SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/..)

add_executable(decode_fuzz source/decode_fuzz.cpp)
target_link_libraries(decode_fuzz PRIVATE Mosaic::Mosaic)
target_compile_options(decode_fuzz PRIVATE -fsanitize=fuzzer,address -g)
target_link_options(decode_fuzz PRIVATE -fsanitize=fuzzer,address)
