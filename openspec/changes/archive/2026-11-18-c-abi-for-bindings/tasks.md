# Tasks

## 1. The ABI

- [x] 1.1 Write `include/mosaic/mosaic_c.h`: opaque handles, value structs, status codes, no C++ types. Verified by: the header
      compiles in a C translation unit and a reviewer can read it in one sitting.
- [x] 1.2 Implement the surface over the C++ core: node lifecycle, publish, follow, pump/tick, record delivery, store open/close,
      transport construction. Verified by: a C test that publishes and tails in one process.
- [x] 1.3 Handle ownership explicitly: one free per handle, double free tolerated, `mosaic_record_clone`/`free` for callers that
      keep bytes. Verified by: tests for double free and for copying a record out of a callback.

## 2. Prove it in C

- [x] 2.1 A C-only test binary that mirrors the CLI's publish/tail through the ABI. Verified by: built with the C compiler, no
      C++ in the test, passing in the suite.
- [x] 2.2 Two-process check over the ABI: publish in one, tail in the other, records in order. Verified by: the same shape as the
      CLI demonstration, run through the ABI only.

## 3. Python

- [x] 3.1 A compiled extension over the ABI with `publish`, `follow`, `pump` and a record iterator. Verified by: a Python test
      that publishes and tails against a second process.
- [x] 3.2 Package it (`pyproject.toml`) with one documented build command and a demo script. Verified by: a clean build in a fresh
      virtualenv and the demo producing records.

## 4. Close out

- [x] 4.1 Add the C API to the overview and module map; mark D2's C ABI promise as kept. Verified by: the doc names the header and
      the ABI test.
- [x] 4.2 Close BIND-1/BIND-2 on the board. Verified by: board and `./scripts/check_todo_ids.sh`.
- [x] 4.3 Definition of done: four build combinations plus the C test, clang-format clean. Verified by: the matrix.
