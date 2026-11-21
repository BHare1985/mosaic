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
if (NOT TARGET MosaicZmq)
    # No ZeroMQ transport in this configuration: its conformance run has nothing to test
    list(FILTER sources EXCLUDE REGEX "test_transport_zmq\\.cpp$")
endif ()
add_executable(${PROJECT_NAME} ${sources})
target_link_libraries(${PROJECT_NAME} doctest::doctest Mosaic::Mosaic)
if (TARGET MosaicZmq)
    target_link_libraries(${PROJECT_NAME} MosaicZmq)
    # One transport test has to speak ZMTP itself - a peer that sends the wrong number of frames
    # cannot be built out of the transport's own API. That needs libzmq's headers in the *test*
    # binary only; the confinement rule (libzmq headers inside the transport) still holds for every
    # product target, because MosaicZmq keeps its own link private.
    foreach (zmq_candidate libzmq-static libzmq ZeroMQ::libzmq)
        if (TARGET ${zmq_candidate})
            get_target_property(zmq_includes ${zmq_candidate} INTERFACE_INCLUDE_DIRECTORIES)
            if (zmq_includes)
                target_include_directories(${PROJECT_NAME} PRIVATE ${zmq_includes})
            endif ()
            break()
        endif ()
    endforeach ()
endif ()
if (TARGET MosaicDaemon)
    target_link_libraries(${PROJECT_NAME} MosaicDaemon)
else ()
    # No czmq here: the configuration tests have nothing to test
    file(GLOB test_sources ${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp)
    list(FILTER sources EXCLUDE REGEX "test_config\\.cpp$")
endif ()
if (TARGET mosaic_sqlite3)
    # The store test corrupts a row directly to prove a damaged tail is survivable.
    target_link_libraries(${PROJECT_NAME} mosaic_sqlite3)
elseif (TARGET SQLite::SQLite3)
    target_link_libraries(${PROJECT_NAME} SQLite::SQLite3)
endif ()
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)

# The anti-drift check: the test compares this with the generated header.
get_target_property(MOSAIC_VERSION_FROM_CMAKE Mosaic INTERFACE_MOSAIC_VERSION)
if (NOT MOSAIC_VERSION_FROM_CMAKE)
    set(MOSAIC_VERSION_FROM_CMAKE "${Mosaic_VERSION}")
endif ()
target_compile_definitions(${PROJECT_NAME} PRIVATE MOSAIC_EXPECTED_VERSION="${MOSAIC_VERSION_FROM_CMAKE}")

# enable compiler warnings
if (NOT TEST_INSTALLED_VERSION)
    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        target_compile_options(Mosaic PUBLIC -Wall -Wpedantic -Wextra -Werror)
    elseif (MSVC)
        target_compile_options(Mosaic PUBLIC /W4 /WX)
        target_compile_definitions(${PROJECT_NAME} PUBLIC DOCTEST_CONFIG_USE_STD_HEADERS)
    endif ()
endif ()

# ---- The C ABI, tested from C ----

# Compiled by a C compiler on purpose: the header must be usable with no C++ in the caller.
if (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/capi/mosaic_c_test.c)
    enable_language(C)
    add_executable(mosaic_c_test ${CMAKE_CURRENT_SOURCE_DIR}/capi/mosaic_c_test.c)
    if (TARGET MosaicC)
        target_link_libraries(mosaic_c_test MosaicC)
    else ()
        target_link_libraries(mosaic_c_test Mosaic::Mosaic)
    endif ()
    set_target_properties(mosaic_c_test PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)

    # The two-process pair: same code, one role each.
    add_executable(mosaic_c_pubtail ${CMAKE_CURRENT_SOURCE_DIR}/capi/mosaic_c_pubtail.c)
    if (TARGET MosaicC)
        target_link_libraries(mosaic_c_pubtail MosaicC)
    else ()
        target_link_libraries(mosaic_c_pubtail Mosaic::Mosaic)
    endif ()
    set_target_properties(mosaic_c_pubtail PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
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
