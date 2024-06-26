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

CPMAddPackage(
        NAME fmt
        GIT_TAG 9.1.0
        GITHUB_REPOSITORY fmtlib/fmt
        OPTIONS "FMT_INSTALL YES" # create an installable target
)

CPMAddPackage(
        NAME libzmq
        GIT_TAG v4.3.5
        GITHUB_REPOSITORY zeromq/libzmq
        OPTIONS
        "BUILD_SHARED OFF"
)

CPMAddPackage(
        NAME czmq
        GIT_TAG v4.2.1
        GITHUB_REPOSITORY zeromq/czmq
        OPTIONS
        "BUILD_SHARED OFF"
        "LIBZMQ_FOUND 1"
        "LIBZMQ_LIBRARIES libzmq-static"
)

CPMAddPackage(
        NAME malamute
        GIT_TAG 1.1.0
        GITHUB_REPOSITORY zeromq/malamute
        OPTIONS
        "BUILD_SHARED OFF"
        "LIBZMQ_FOUND 1"
        "LIBZMQ_LIBRARIES libzmq-static"
        "CZMQ_FOUND 1"
        "CZMQ_LIBRARIES czmq-static"
)

# ---- Add source files ----

# Note: globbing sources is considered bad practice as CMake's generators may not detect new files
# automatically. Keep that in mind when changing files, or explicitly mention them here.
file(GLOB_RECURSE headers CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h")
file(GLOB_RECURSE sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp")

# ---- Create library ----

# Note: for header-only libraries change all PUBLIC flags to INTERFACE and create an interface
# target: add_library(${PROJECT_NAME} INTERFACE)
add_library(${PROJECT_NAME} ${headers} ${sources})
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 17)

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
target_link_libraries(${PROJECT_NAME} PRIVATE fmt::fmt mlm-static)

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
        DEPENDENCIES "fmt 9.1.0"
)
