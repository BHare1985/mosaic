# AGENTS.md

## Goal

Mosaic is a **brokerless, verifiable, replicated-log library plus daemon** in C++20 on ZeroMQ: signed hash-chained
append-only logs per channel, pattern subscriptions, peer-to-peer replication and repair, credit-based flow control, and an
optional job layer. It is inspired by Dafka and Malamute (not wire-compatible). Settled decisions:
[docs/design/decisions.md](docs/design/decisions.md). Rationale: [docs/direction.md](docs/direction.md).

The original vision (parity coding, proof of replication, tokens, chain) is an optional future layer on top, not core scope.

## Ground rules

- **Keep everything local** (decision D4): no pushes, PRs or issues, and nothing filed or patched upstream (libzmq, czmq,
  malamute, dafka, ...). Log findings in [docs/upstream-notes.md](docs/upstream-notes.md).
- Commit locally only when asked. Commit author: use the maintainer's identity per command, never edit git config.
- Work from the task board: [docs/design/TODO.md](docs/design/TODO.md). Respect the module dependency order.
- Reference copies of upstream material: `docs/references/` (run `docs/references/fetch.sh`; git-ignored).

## Layout

- `include/mosaic/<module>/` + `source/<module>/` – library, one dir per module (`core store log match flow proto net node jobs`).
  Each module has a `README.md` with rules and open tasks. Dependency order: core ← store ← log ← proto ← net ← node ← jobs;
  `match` and `flow` are leaves.
- `standalone/` – `Mosaic` CLI (`version`, `match`)
- `test/source/` – doctest suite (`MosaicTests`)
- `docs/design/` – **design set and junior TODO board (`TODO.md`)**; read `00-overview.md` and `conventions.md` first
- `docs/references/` – fetched upstream material (git-ignored)
- `scripts/check_todo_ids.sh` – every `TODO(ID)` in code must exist in `docs/design/TODO.md`

## Commands

```bash
# fast loop (no ZeroMQ stack)
cmake -S test -B build/test -DMOSAIC_WITH_ZMQ=OFF -DMOSAIC_BUILD_ZMQ_FROM_SOURCE=OFF && cmake --build build/test -j && ./build/test/MosaicTests
# everything, incl. libzmq and czmq from source
cmake -S all -B build && cmake --build build -j && ./build/all/test/MosaicTests
./scripts/check_todo_ids.sh
```

## Conventions

See `docs/design/conventions.md`: C++20, warnings are errors, no exceptions in core..node, time and randomness injected,
single-threaded, no dependencies in core..node, every interface has a contract comment and (eventually) a conformance suite.

## References

- Dafka: https://github.com/zeromq/dafka (RFC 46: https://rfc.zeromq.org/spec/46) - design reference only; nothing is
  built or linked from it
- ZeroMQ Guide: https://zguide.zeromq.org · ZeroMQ: https://zeromq.org
