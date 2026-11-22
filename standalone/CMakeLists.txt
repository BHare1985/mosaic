cmake_minimum_required(VERSION 3.14...3.22)

project(MosaicStandalone LANGUAGES CXX)

# let old third-party CMake projects configure under CMake >= 4
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

# --- Import tools ----

include(../cmake/tools.cmake)

# ---- Dependencies ----

include(../cmake/CPM.cmake)

CPMAddPackage(NAME Mosaic SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/..)

# ---- Create standalone executables ----

# The CLI excludes the daemon entry point; mosaicd is its own binary and needs czmq (ZPL config).
file(GLOB sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp)
list(FILTER sources EXCLUDE REGEX "mosaicd\\.cpp$")

add_executable(${PROJECT_NAME} ${sources})

set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON OUTPUT_NAME "Mosaic")

target_link_libraries(${PROJECT_NAME} Mosaic::Mosaic)
# The CLI is the operator surface for real links, so it needs the ZeroMQ transport.
if (TARGET MosaicZmq)
    target_link_libraries(${PROJECT_NAME} MosaicZmq)
endif ()

if (TARGET MosaicDaemon)
    # The CLI reads the same ZPL file as the daemon, so it needs the config parser too.
    target_link_libraries(${PROJECT_NAME} MosaicDaemon)
    add_executable(mosaicd ${CMAKE_CURRENT_SOURCE_DIR}/source/mosaicd.cpp)
    set_target_properties(mosaicd PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
    target_link_libraries(mosaicd MosaicDaemon)
endif ()
