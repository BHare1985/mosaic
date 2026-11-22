# Proposal

## Why

Everything works and only C++ can use it. The direction document promised "a C++20 library with a stable C ABI, so language
bindings follow" (D2), and that is the last structural thing standing between this and anyone who is not writing C++: Python for
scripts and data work, Go/Rust for services, and anything with an FFI.

A C ABI is also a forcing function: it makes the ownership and lifetime rules explicit (who frees a record, who owns a node) in a
way a C++ header can leave implicit. Doing it after the authorization work is deliberate - it freezes the API, so the API should
be the one that survived a security review.

## What Changes

- A thin C ABI over the C++ core: opaque handles for node, store, transport; value structs for records and results; explicit
  free functions. No C++ types, no exceptions, no templates, no ownership ambiguity.
- A Python binding over that ABI, distributed as a wheel with a compiled extension, demonstrating publish and tail through the
  same code path the CLI uses.
- An ABI test written in C (not C++), so the header is proven usable from a C compiler and a C-only caller.
- **BREAKING** in the sense that the ABI, once published, must stay compatible: additions only, no signature changes.
- **Not** in scope: bindings for every language, a network protocol for other runtimes, or exposing the job layer in this
  change (a second pass once the core surface is stable).

## Capabilities

### New Capabilities

- `api/c-abi`: the stable C surface - handle lifetimes, error reporting, and the guarantee that a caller written in C can do what
  the CLI does.

## Impact

- `include/mosaic/mosaic_c.h` (C only, no C++ headers), `source/capi/mosaic_c.cpp`.
- `python/` package with `pyproject.toml`, a CFFI/ctypes-free extension over the ABI, and a demo that publishes and tails.
- Tests: a C-only test binary compiled with a C compiler, plus a Python test that runs two processes.
- `docs/design/00-overview.md` and the module map gain the C API; D2's C ABI promise stops being a promise.
- `docs/design/TODO.md`: BIND-1, BIND-2 close.
