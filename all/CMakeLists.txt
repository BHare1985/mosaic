# this script adds all subprojects to a single build to allow IDEs understand the full project
# structure.

cmake_minimum_required(VERSION 3.14...3.22)

project(BuildAll LANGUAGES CXX)

# let old third-party CMake projects configure under CMake >= 4
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

include(../cmake/tools.cmake)

# needed to generate test target
enable_testing()

# Outputs live under build/all/ so a full-stack build can never overwrite the fast loop's
# build/test/MosaicTests. Before this, whichever built last silently won and a "full stack" run could
# execute the core-only binary and report a green that never touched ZeroMQ.
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../standalone ${CMAKE_BINARY_DIR}/all/standalone)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../test ${CMAKE_BINARY_DIR}/all/test)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../docs/site ${CMAKE_BINARY_DIR}/all/site)
