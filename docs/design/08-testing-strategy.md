# 08 — Testing strategy & definition of done

## Layers (all run in `MosaicTests`, no network, no sleeps)
| Layer | Example today | Purpose |
|---|---|---|
| Unit | `test_core.cpp` (SHA-256 vectors, canonical bytes), `test_log.cpp` | pure logic |
| Reference/property | `test_matcher.cpp` randomized vs naive matcher | catch index bugs |
| Contract/conformance | `MemoryLogStore contract` (to become a reusable suite: STORE-0, NET-0) | every backend/transport behaves the same |
| Simulation | `test_node_sim.cpp` (late joiner, 30% loss + 10% dup convergence, forged records) | whole-system properties |
| Fuzz | seeded mutation test over every message (`test_codec.cpp`), plus an opt-in libFuzzer target in `fuzz/` | hostile input |

## The headline property
> Whatever the network does (loss, duplication, delay, reorder, partitions), after it heals every follower's log is identical
> to the writer's, and no unverified record is ever stored or delivered.

Every protocol or Node change must keep `converges under 30% loss and 10% duplication` green and add a scenario for the new
behaviour. SIM-2 turns this into a seed sweep so failures print a reproducible seed.

## Definition of done (any task)
1. Header doc comment states the contract; module README updated; design doc row updated (✅/🟡/⬜).
2. Unit tests + (if it touches Node/proto/net) a SimNetwork scenario.
3. Builds warning-free with ZeroMQ **on and off** and with libsodium on and off; all tests pass; `clang-format` clean.
4. No new dependency in `core`..`node`; no ZeroMQ headers outside the ZeroMQ transport.
5. Every `TODO(ID)` you leave behind exists in [TODO.md](TODO.md).

## Commands
```bash
cmake -S test -B build/test -DMOSAIC_WITH_ZMQ=OFF -DMOSAIC_BUILD_ZMQ_FROM_SOURCE=OFF && cmake --build build/test -j && ./build/test/MosaicTests   # fast loop
cmake -S all -B build && cmake --build build -j && ./build/all/test/MosaicTests                                       # full, with ZeroMQ stack
./build/test/MosaicTests -tc="*converges*"                        # one test                                                                           # one test
```
