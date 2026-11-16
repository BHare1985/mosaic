cmake_minimum_required(VERSION 3.14...3.22)

# ---- Project ----

# Note: update this to your new project's name and version
project(
        Mosaic
        VERSION 0.1.0
        LANGUAGES CXX
)

# ---- Include guards ----

if (PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(
            FATAL_ERROR
            "In-source builds not allowed. Please make a new directory (called a build directory) and run CMake from there."
    )
endif ()

# ---- Add dependencies via CPM ----
# see https://github.com/TheLartians/CPM.cmake for more info

include(cmake/CPM.cmake)

# PackageProject.cmake will be used to make our target installable
CPMAddPackage("gh:TheLartians/PackageProject.cmake@1.8.0")

# ZeroMQ is a hard dependency of the PRODUCT (the network transport, ZMQ-1), but the core
# library is deliberately ZeroMQ-free so the protocol can be tested in-process with SimNetwork.
# This option only controls whether libzmq/czmq/malamute are COMPILED FROM SOURCE here (slow;
# also the "does the ZeroMQ ecosystem still build" check). Use -DMOSAIC_BUILD_ZMQ_STACK=OFF for a
# fast core-only loop. ZMQ-0 will replace this with a proper MosaicZmq target and find_package.
option(MOSAIC_BUILD_ZMQ_STACK "Build libzmq/czmq/malamute from source" ON)

# libsodium provides BLAKE2b and Ed25519 behind the Hasher/Signer/Verifier seams (CORE-1, CORE-2).
# With it OFF the core still builds and tests on the dependency-free SHA-256 plus a test signer, so
# the "no third-party dependency in core..node" build stays available. libsodium is fetched the same
# way this repo has always fetched it (see the 2024 key-loading work): pinned CPM package.
option(MOSAIC_WITH_SODIUM "Use libsodium for BLAKE2b and Ed25519" ON)

if (MOSAIC_WITH_SODIUM)
    CPMAddPackage(
            NAME libsodium
            GITHUB_REPOSITORY robinlinden/libsodium-cmake
            GIT_TAG f568ff02f1bed155ea598c0e803ef3c9db2703d2
            GIT_SUBMODULES_RECURSE TRUE
            OPTIONS
            "SODIUM_DISABLE_TESTS ON"
            "BUILD_SHARED_LIBS OFF"
    )
endif ()

if (MOSAIC_BUILD_ZMQ_STACK)
    CPMAddPackage(
            NAME libzmq
            GIT_TAG v4.3.5
            GITHUB_REPOSITORY zeromq/libzmq
            OPTIONS
            "BUILD_SHARED OFF"
    )

    # czmq/malamute find libzmq/czmq headers via these dirs (their find modules are bypassed below)
    include_directories(${libzmq_SOURCE_DIR}/include)

    CPMAddPackage(
            NAME czmq
            GIT_TAG v4.2.1
            GITHUB_REPOSITORY zeromq/czmq
            OPTIONS
            "BUILD_SHARED OFF"
            "LIBZMQ_FOUND 1"
            "LIBZMQ_LIBRARIES libzmq-static"
    )

    include_directories(${czmq_SOURCE_DIR}/include)

    CPMAddPackage(
            NAME malamute
            GIT_TAG 1.1.0
            GITHUB_REPOSITORY zeromq/malamute
            # malamute's "distclean" target collides with czmq's (same global name)
            PATCH_COMMAND sed -i "s/distclean/mlm_distclean/g" CMakeLists.txt
            OPTIONS
            "BUILD_SHARED OFF"
            "LIBZMQ_FOUND 1"
            "LIBZMQ_LIBRARIES libzmq-static"
            "CZMQ_FOUND 1"
            "CZMQ_LIBRARIES czmq-static"
    )
endif ()

# ---- Add source files ----

# Note: globbing sources is considered bad practice as CMake's generators may not detect new files
# automatically. Keep that in mind when changing files, or explicitly mention them here.
file(GLOB_RECURSE headers CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h")
file(GLOB_RECURSE sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp")

# ---- Create library ----

# Note: for header-only libraries change all PUBLIC flags to INTERFACE and create an interface
# target: add_library(${PROJECT_NAME} INTERFACE)
add_library(${PROJECT_NAME} ${headers} ${sources})
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_20)

if (MSVC)
    target_compile_definitions(${PROJECT_NAME} PUBLIC
            # FIXME: Silence MSVC warnings about use of stdext::checked_array_iterator until migration to C++20:
            #        warning STL4043: stdext::checked_array_iterator, stdext::unchecked_array_iterator,
            #        and related factory functions are non-Standard extensions and will be removed in the future.
            #        std::span (since C++20) and gsl::span can be used instead.
            _SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING
    )
endif ()

# being a cross-platform target, we enforce standards conformance on MSVC
target_compile_options(${PROJECT_NAME} PUBLIC "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/permissive->")

# Link dependencies
# The core library has no third-party dependencies except the crypto seam: libsodium sits behind
# Hasher/Signer/Verifier and never appears in a public header (see docs/design/00-overview.md).
if (MOSAIC_WITH_SODIUM)
    if (TARGET sodium)
        target_compile_definitions(${PROJECT_NAME} PUBLIC MOSAIC_HAS_SODIUM)
        # BUILD_INTERFACE keeps the third-party target out of our install(EXPORT); when this library
        # is actually packaged for downstream consumers, libsodium becomes an exported requirement.
        target_link_libraries(${PROJECT_NAME} PRIVATE $<BUILD_INTERFACE:sodium>)
    else ()
        message(WARNING "MOSAIC_WITH_SODIUM=ON but libsodium did not produce a `sodium` target; "
                "falling back to the dependency-free crypto build.")
        set(MOSAIC_WITH_SODIUM OFF)
    endif ()
endif ()

target_include_directories(
        ${PROJECT_NAME} PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include/${PROJECT_NAME}-${PROJECT_VERSION}>
)

# ---- Create an installable target ----
# this allows users to install and find the library via `find_package()`.

# the location where the project's version header will be placed should match the project's regular
# header paths
string(TOLOWER ${PROJECT_NAME}/version.h VERSION_HEADER_LOCATION)

packageProject(
        NAME ${PROJECT_NAME}
        VERSION ${PROJECT_VERSION}
        NAMESPACE ${PROJECT_NAME}
        BINARY_DIR ${PROJECT_BINARY_DIR}
        INCLUDE_DIR ${PROJECT_SOURCE_DIR}/include
        INCLUDE_DESTINATION include/${PROJECT_NAME}-${PROJECT_VERSION}
        VERSION_HEADER "${VERSION_HEADER_LOCATION}"
        COMPATIBILITY SameMajorVersion
)
