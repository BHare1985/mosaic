cmake_minimum_required(VERSION 3.14...3.22)

project(MosaicTests LANGUAGES CXX)

# let old third-party CMake projects configure under CMake >= 4
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

# ---- Options ----

option(ENABLE_TEST_COVERAGE "Enable test coverage" OFF)
option(TEST_INSTALLED_VERSION "Test the version found by find_package" OFF)

# --- Import tools ----

include(../cmake/tools.cmake)

# ---- Dependencies ----

include(../cmake/CPM.cmake)

CPMAddPackage("gh:doctest/doctest@2.4.11")

#clang-format target conflicts with libzmq
#CPMAddPackage("gh:TheLartians/Format.cmake@1.7.3")

if (TEST_INSTALLED_VERSION)
    find_package(Mosaic REQUIRED)
else ()
    CPMAddPackage(NAME Mosaic SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/..)
endif ()

# ---- Create binary ----

file(GLOB sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp)
if (NOT TARGET mlm-static)
    # ZeroMQ ecosystem not built here: skip its compile-check test
    list(FILTER sources EXCLUDE REGEX "malamute\\.cpp$")
endif ()
if (NOT TARGET MosaicZmq)
    # No ZeroMQ transport in this configuration: its conformance run has nothing to test
    list(FILTER sources EXCLUDE REGEX "test_transport_zmq\\.cpp$")
endif ()
add_executable(${PROJECT_NAME} ${sources})
target_link_libraries(${PROJECT_NAME} doctest::doctest Mosaic::Mosaic)
if (TARGET MosaicZmq)
    target_link_libraries(${PROJECT_NAME} MosaicZmq)
endif ()
if (TARGET mlm-static)
    target_link_libraries(${PROJECT_NAME} mlm-static)
endif ()
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)

# enable compiler warnings
if (NOT TEST_INSTALLED_VERSION)
    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        target_compile_options(Mosaic PUBLIC -Wall -Wpedantic -Wextra -Werror)
    elseif (MSVC)
        target_compile_options(Mosaic PUBLIC /W4 /WX)
        target_compile_definitions(${PROJECT_NAME} PUBLIC DOCTEST_CONFIG_USE_STD_HEADERS)
    endif ()
endif ()

# ---- Add MosaicTests ----

enable_testing()

# Note: doctest and similar testing frameworks can automatically configure CMake tests. For other
# testing frameworks add the tests target instead: add_test(NAME ${PROJECT_NAME} COMMAND
# ${PROJECT_NAME})

include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
doctest_discover_tests(${PROJECT_NAME})

# ---- code coverage ----

if (ENABLE_TEST_COVERAGE)
    target_compile_options(Mosaic PUBLIC -O0 -g -fprofile-arcs -ftest-coverage)
    target_link_options(Mosaic PUBLIC -fprofile-arcs -ftest-coverage)
endif ()
