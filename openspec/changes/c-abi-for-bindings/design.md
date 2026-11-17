# Design

## Context

See proposal.md. The C++ core is 4.4k lines across ten modules with interfaces for store, transport and crypto; the CLI already
exercises the full path (publish/tail/verify) so the ABI's sufficiency can be tested by reimplementing the CLI over it. Python is
the first binding because it needs nothing beyond a C compiler and pip.

## Goals / Non-Goals

**Goals:** a C header a C programmer can read in one sitting; ownership that cannot be got wrong silently; a Python package that
proves the surface is enough.

**Non-Goals:** covering the job layer in the first pass, async callbacks that outlive the loop, ABI stability tooling beyond
"additions only", or bindings for more than one language.

## Decisions

**D1 - Opaque handles plus value structs, no unions of callbacks.**
`mosaic_node`, `mosaic_store`, `mosaic_transport` are opaque; records and results are plain structs of pointers and lengths. Errors
are `mosaic_status` enum values plus an optional message the caller frees. *Alternative:* a struct-of-function-pointers "class"
like czmq - rejected: it is more indirection for less type safety.

**D2 - The ABI mirrors the loop, not the internals.**
`mosaic_node_pump`, `mosaic_node_tick`, `mosaic_node_publish`, `mosaic_node_follow`, `mosaic_node_poll_record` - the same shape as
the C++ API, because hiding the loop would mean owning a thread, and this library's whole testability story is "the caller owns
time".

**D3 - Records are borrowed for the callback, copies are explicit.**
A record handed to a callback is valid only during that call; a `mosaic_record_clone`/`mosaic_record_free` pair exists for callers
that need to keep bytes. This avoids the classic FFI bug where a binding keeps a pointer into a buffer that the library reuses.

**D4 - The Python binding is a compiled extension over the ABI, not a reimplementation.**
One code path, so a bug found through Python is a bug in the library. *Alternative:* a subprocess wrapper around the CLI -
rejected: it cannot expose per-record callbacks without parsing text.

**D5 - The ABI test is written in C.**
A C++ test would not prove the header is C-usable. The test binary compiles with the C compiler and does publish/tail through the
ABI only.

## Risks / Trade-offs

- **An ABI is a promise** -> additions only; the test asserts the documented surface and struct layouts are what a release
  shipped.
- **Handle lifetimes are the hard part** -> one owner per handle, and the free functions tolerate a second call; the tests cover
  double free and use-after-free shapes that are detectable.
- **Python packaging pulls in build tooling** -> keep the extension small and document the one command that builds it; no cffi
  code generation at import time.

## Open Questions

- Whether the ABI should expose the job layer before or after a Python job demo exists. Deferrable: core surface first.
- Whether to publish prebuilt wheels. Deferrable: a source build is enough to prove the surface.
