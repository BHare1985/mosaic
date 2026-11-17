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

# ---- Crypto dependency (CORE-1, CORE-2) ----
# libsodium provides BLAKE2b and Ed25519 behind the Hasher/Signer/Verifier seams. With it OFF the core
# still builds and tests on the dependency-free SHA-256 plus a test signer, so the "no third-party
# dependency in core..node" build stays available. Fetched the way this repo already did it in 2024
# (branch key-loading): pinned CPM package.
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
            # czmq builds a shared library that links this static libsodium, so it has to be PIC.
            "CMAKE_POSITION_INDEPENDENT_CODE ON"
    )
endif ()

# ZeroMQ carries the transport (ZMQ-1). It is confined to the MosaicZmq target, so the core library and
# its tests build and pass with no ZeroMQ installed at all. MOSAIC_BUILD_ZMQ_FROM_SOURCE decides only
# whether this build also compiles the ecosystem from source, which is slow and is also the
# "does ZeroMQ still build in 2026" check.
option(MOSAIC_BUILD_ZMQ_FROM_SOURCE "Build libzmq and czmq from source (slow; use a system copy when you have one)" ON)
option(MOSAIC_WITH_ZMQ "Build the ZeroMQ transport (MosaicZmq)" ON)

set(MOSAIC_ZMQ_TARGET "")

if (MOSAIC_WITH_ZMQ)
    # Prefer an installed libzmq; fall back to building it here.
    find_package(ZeroMQ QUIET)
    find_package(libzmq QUIET)

    if (TARGET libzmq-static)
        set(MOSAIC_ZMQ_TARGET libzmq-static)
    elseif (TARGET libzmq)
        set(MOSAIC_ZMQ_TARGET libzmq)
    elseif (TARGET ZeroMQ::libzmq)
        set(MOSAIC_ZMQ_TARGET ZeroMQ::libzmq)
    elseif (MOSAIC_BUILD_ZMQ_FROM_SOURCE)
        # libzmq cannot build CURVE without libsodium, so a build without the crypto dependency gets a
        # libzmq without CURVE rather than a build failure. curve_available() then reports false and
        # nothing pretends to be encrypted.
        if (MOSAIC_WITH_SODIUM)
            # libzmq's generated Findsodium.cmake requires pkg-config before it looks for sodium, and its
            # find_path/find_library calls skip work when these are already set. A stub pkg-config plus
            # explicit paths makes a CPM-fetched libsodium usable; a system libzmq needs none of this.
            # Logged in docs/upstream-notes.md.
            set(PKG_CONFIG_EXECUTABLE "${PROJECT_SOURCE_DIR}/cmake/no-pkg-config.sh"
                    CACHE FILEPATH "stub: see cmake/no-pkg-config.sh" FORCE)
            set(SODIUM_INCLUDE_DIRS "${libsodium_SOURCE_DIR}/libsodium/src/libsodium/include"
                    CACHE PATH "libsodium headers fetched by CPM" FORCE)
            set(SODIUM_LIBRARIES "${libsodium_BINARY_DIR}/libsodium.a"
                    CACHE FILEPATH "libsodium built by CPM" FORCE)
            set(MOSAIC_ZMQ_CURVE_OPTIONS "ENABLE_CURVE ON" "WITH_LIBSODIUM ON" "WITH_LIBSODIUM_STATIC ON")
        else ()
            set(MOSAIC_ZMQ_CURVE_OPTIONS "ENABLE_CURVE OFF")
        endif ()
        CPMAddPackage(
                NAME libzmq
                GIT_TAG v4.3.5
                GITHUB_REPOSITORY zeromq/libzmq
                OPTIONS
                "BUILD_SHARED OFF"
                # SEC-1: CURVE only when a crypto backend is present (see above).
                ${MOSAIC_ZMQ_CURVE_OPTIONS}
        )
        if (TARGET libzmq-static)
            set(MOSAIC_ZMQ_TARGET libzmq-static)
        elseif (TARGET libzmq)
            set(MOSAIC_ZMQ_TARGET libzmq)
        endif ()
    endif ()

    if (NOT MOSAIC_ZMQ_TARGET)
        message(WARNING "MOSAIC_WITH_ZMQ=ON but no libzmq was found and MOSAIC_BUILD_ZMQ_FROM_SOURCE=OFF; "
                "building only the core library.")
        set(MOSAIC_WITH_ZMQ OFF)
    elseif (TARGET sodium AND MOSAIC_ZMQ_TARGET)
        # CURVE links libsodium, which CPM fetched: order the build so it exists first.
        add_dependencies(${MOSAIC_ZMQ_TARGET} sodium)
    endif ()
endif ()

# czmq is used by the daemon and CLI targets only (ZPL config, discovery). It never reaches core..node or the
# `core` / `no-crypto` configurations. The old "does the Malamute/Dafka ecosystem still build" track is gone:
# this project does not depend on that ecosystem, so proving it compiles was work without a beneficiary.
option(MOSAIC_WITH_CZMQ "Build the daemon (MosaicDaemon), which uses czmq for ZPL config" ON)

if ((MOSAIC_WITH_CZMQ OR MOSAIC_BUILD_ZMQ_FROM_SOURCE) AND MOSAIC_ZMQ_TARGET)
    include_directories(${CMAKE_BINARY_DIR}/_deps/libzmq-src/include)
    CPMAddPackage(
            NAME czmq
            GIT_TAG v4.2.1
            GITHUB_REPOSITORY zeromq/czmq
            OPTIONS
            "BUILD_SHARED OFF"
            "LIBZMQ_FOUND 1"
            "LIBZMQ_LIBRARIES ${MOSAIC_ZMQ_TARGET}"
    )

    include_directories(${czmq_SOURCE_DIR}/include)
endif ()

# ---- SQLite LogStore backend (STORE-1) ----
# Persistence behind the LogStore seam. Prefer a system SQLite; otherwise fetch the amalgamation, which
# is what makes this dependency-free project able to have a durable backend without asking a developer
# to install anything. Off = the memory store remains the only backend.
option(MOSAIC_WITH_SQLITE "Build the SQLite LogStore backend" ON)
set(MOSAIC_SQLITE_TARGET "")

if (MOSAIC_WITH_SQLITE)
    find_package(SQLite3 QUIET)
    if (TARGET SQLite::SQLite3)
        set(MOSAIC_SQLITE_TARGET SQLite::SQLite3)
    else ()
        include(FetchContent)
        FetchContent_Declare(sqlite_amalgamation
                URL https://sqlite.org/2024/sqlite-amalgamation-3460100.zip
                DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        FetchContent_MakeAvailable(sqlite_amalgamation)
        enable_language(C) # sqlite3.c is C, and this project is otherwise CXX-only
        add_library(mosaic_sqlite3 STATIC ${sqlite_amalgamation_SOURCE_DIR}/sqlite3.c)
        target_include_directories(mosaic_sqlite3 PUBLIC ${sqlite_amalgamation_SOURCE_DIR})
        target_compile_definitions(mosaic_sqlite3 PRIVATE SQLITE_THREADSAFE=1)
        set(MOSAIC_SQLITE_TARGET mosaic_sqlite3)
    endif ()
    message(STATUS "SQLite LogStore: ${MOSAIC_SQLITE_TARGET}")
endif ()

# ---- Add source files ----

# Note: globbing sources is considered bad practice as CMake's generators may not detect new files
# automatically. Keep that in mind when changing files, or explicitly mention them here.
file(GLOB_RECURSE headers CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h")
file(GLOB_RECURSE sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp")
# The ZeroMQ transport is the one place ZeroMQ may appear; keeping it out of this target is what makes
# "the core builds without ZeroMQ" a property rather than a hope.
list(FILTER sources EXCLUDE REGEX "source/net/zmq_transport\\.cpp$")
# The daemon (ZPL config via czmq today) and anything else that needs czmq are their own targets, never
# part of the core library.
list(FILTER sources EXCLUDE REGEX "source/daemon/")
list(FILTER sources EXCLUDE REGEX "source/net/czmq_")
if (NOT MOSAIC_SQLITE_TARGET)
    # No SQLite available: the durable backend is not part of this build.
    list(FILTER sources EXCLUDE REGEX "source/store/sqlite_log_store\\.cpp$")
endif ()

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
if (MOSAIC_SQLITE_TARGET)
    target_compile_definitions(${PROJECT_NAME} PUBLIC MOSAIC_HAS_SQLITE)
    target_link_libraries(${PROJECT_NAME} PRIVATE $<BUILD_INTERFACE:${MOSAIC_SQLITE_TARGET}>)
endif ()

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

# ---- ZeroMQ transport target ----

if (MOSAIC_WITH_ZMQ AND MOSAIC_ZMQ_TARGET)
    add_library(MosaicZmq source/net/zmq_transport.cpp include/mosaic/net/zmq_transport.h)
    target_compile_features(MosaicZmq PUBLIC cxx_std_20)
    target_link_libraries(MosaicZmq PUBLIC ${PROJECT_NAME} PRIVATE ${MOSAIC_ZMQ_TARGET})
    target_compile_definitions(MosaicZmq PUBLIC MOSAIC_HAS_ZMQ)
    target_include_directories(
            MosaicZmq PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include/${PROJECT_NAME}-${PROJECT_VERSION}>
    )
endif ()

# ---- Daemon target (czmq: ZPL config; discovery next) ----

# The daemon verifies records, so it needs real signatures: without libsodium there is nothing to verify
# with (the only signer left is a test double). The core library keeps building either way.
if (MOSAIC_WITH_CZMQ AND MOSAIC_WITH_SODIUM AND TARGET czmq-static AND TARGET sodium)
    add_library(MosaicDaemon source/daemon/config.cpp source/daemon/daemon.cpp source/net/czmq_discovery.cpp)
    target_compile_features(MosaicDaemon PUBLIC cxx_std_20)
    target_link_libraries(MosaicDaemon PUBLIC ${PROJECT_NAME} PRIVATE czmq-static)
    if (TARGET MosaicZmq)
        target_link_libraries(MosaicDaemon PUBLIC MosaicZmq)
    endif ()
    target_compile_definitions(MosaicDaemon PUBLIC MOSAIC_HAS_CZMQ)
    target_include_directories(
            MosaicDaemon PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
                                $<BUILD_INTERFACE:${czmq_SOURCE_DIR}/include>
                                $<INSTALL_INTERFACE:include/${PROJECT_NAME}-${PROJECT_VERSION}>
    )
endif ()

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
