# AGENTS.md

## Goal

Mosaic is a **Decentralized Data Parity Broker**: high-performance distribution of verifiable, repairable fountain-code parity symbols across a network of storage nodes, with a blockchain-based consensus layer targeting >100,000 TPS via distributed message-queue technology. The aim is a barebones infrastructure capable of supporting decentralized CDNs while staying adaptable to specific use cases.

Key ideas:
- **Proof of Replication & Geolocation** – inspired by Filecoin; geolocation per https://eprint.iacr.org/2021/697.pdf
- **Two utility tokens** – bandwidth and storage; contributors earn per byte provided
- **Scalability protocol** – built on ZeroMQ messaging; depends on [Malamute](https://github.com/zeromq/malamute) (C)
- **Permissioned blockchain initially** (storage nodes are permissionless); to become permissionless once malicious-actor mitigations exist

## Direction (under discussion)

See [docs/direction.md](docs/direction.md): proposed pivot to a small brokerless,
Dafka-inspired replicated-log library. Design in
[docs/decentralized-broker-2026.md](docs/decentralized-broker-2026.md).
**Do not fix or file anything upstream (libzmq, czmq, malamute, dafka, ...) without
discussing with the maintainer first**; log findings in
[docs/upstream-notes.md](docs/upstream-notes.md).
Reference copies: `docs/references/` (run `docs/references/fetch.sh`; git-ignored).

## References

- Repo: https://github.com/BHare1985/mosaic
- Docs: https://bhare1985.github.io/mosaic (Doxygen, published on GitHub Release)
- Proof of geolocation paper: https://eprint.iacr.org/2021/697.pdf
- Malamute: https://github.com/zeromq/malamute
- ZeroMQ: https://zeromq.org
- Filecoin (proof of replication inspiration): https://filecoin.io
- Project template: modern CMake, CPM.cmake, Format.cmake, PackageProject.cmake (see README)

## Layout

- `include/mosaic/<module>/` + `source/<module>/` – library, one dir per module (`core store log match flow proto net node jobs`).
  Each module has a `README.md` with rules and open tasks. Dependency order: core ← store ← log ← proto ← net ← node ← jobs;
  `match` and `flow` are leaves.
- `standalone/` – `Mosaic` CLI (`version`, `match`)
- `test/source/` – doctest suite (`MosaicTests`); `malamute.cpp` is the ZeroMQ-stack compile check
- `docs/design/` – **design set and junior TODO board (`TODO.md`)**; read `00-overview.md` and `conventions.md` first
- `docs/references/` – fetched upstream material (git-ignored)
- `scripts/check_todo_ids.sh` – every `TODO(ID)` in code must exist in `docs/design/TODO.md`

## Commands

```bash
# fast loop (no ZeroMQ stack)
cmake -S test -B build/test -DMOSAIC_BUILD_ZMQ_STACK=OFF && cmake --build build/test -j && ./build/test/MosaicTests
# everything, incl. libzmq/czmq/malamute from source
cmake -S all -B build && cmake --build build -j && ./build/test/MosaicTests
./scripts/check_todo_ids.sh
```

## Conventions

See `docs/design/conventions.md`: C++20, warnings are errors, no exceptions in core..node, time and randomness injected,
single-threaded, no dependencies in core..node, every interface has a contract comment and (eventually) a conformance suite.
