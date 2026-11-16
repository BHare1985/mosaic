# Upstream issues found while reviving the build (2026-11)

**Policy:** nothing below has been filed or patched upstream. Each item needs a
discussion with the maintainer first. Where the build needed a workaround, it
lives in this repo only, and is listed under "local workaround".

Toolchain: GCC 15.2, CMake 4.2.3, Ubuntu (Linux 7.0).

| # | Component | Symptom | Local workaround | Upstream candidate |
|---|---|---|---|---|
| 1 | malamute 1.1.0 `CMakeLists.txt:590` | `add_custom_command(TARGET distclean ...)` errors "TARGET 'distclean' was not created in this directory": czmq already created a global `distclean` target in its own directory, so malamute's `if (NOT TARGET distclean)` skips creating one. Breaks any project that pulls czmq and malamute into one CMake tree. | `sed` renames it to `mlm_distclean` via CPM `PATCH_COMMAND` (top-level `CMakeLists.txt`) | Yes: guard on directory, or prefix target names. Same pattern likely in czmq. |
| 2 | czmq 4.2.1 / malamute 1.1.0 | With `LIBZMQ_FOUND=1` / `CZMQ_FOUND=1` forced (as this repo does to skip their find modules), `zmq.h` / `czmq.h` are not on the include path. May be our bypass rather than an upstream bug. | `include_directories(${libzmq_SOURCE_DIR}/include)` and czmq equivalent | Unclear: investigate whether `libzmq-static` / `czmq-static` should export include dirs. |
| 3 | malamute `src/mlm_classes.h:71-81` | Warnings: `mlm_msg_test`, `mlm_stream_simple_test`, `mlm_mailbox_bounded_test` redeclared with different visibility | none (warnings only; we scope `-Werror` to our targets) | Yes, trivial |
| 4 | doctest 2.4.9 (confirmed); possibly others | `cmake_minimum_required` < 3.5 is rejected by CMake 4 | `CMAKE_POLICY_VERSION_MINIMUM 3.5` in our CMake files; doctest bumped to 2.4.11. Not checked which of libzmq/czmq/malamute still need the policy flag. | Check each against master |
| 5 | fmt 9.1.0 | `-Werror=dangling-reference` on GCC 15 | bumped to 11.0.2 | n/a (fixed upstream) |
| 6 | cxxopts 3.0.0 – 3.2.1 | Missing `#include <cstdint>` (`uint8_t`, `intmax_t`) on GCC 15 | include `<cstdint>` before cxxopts in `standalone/source/main.cpp` | Check cxxopts master / file issue if still present |
| 7 | CPM.cmake 0.38.5 (`cmake/CPM.cmake`) | Uses deprecated `FetchContent_Populate` (CMP0169 warning) | none (warning) | Bump CPM.cmake |
| 8 | dafka | Last commit 2020-09-11; depends on LevelDB and old czmq draft APIs. **Not yet built on this toolchain.** | n/a | Unknown: first step is to try building it |

Repo-local fixes that are ours, not upstream's: freed a leaked `char*` in
`test/source/malamute.cpp`; replaced tabs with spaces.
