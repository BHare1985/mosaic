# AGENTS.md

## Goal

Mosaic is a **Decentralized Data Parity Broker**: high-performance distribution of verifiable, repairable fountain-code parity symbols across a network of storage nodes, with a blockchain-based consensus layer targeting >100,000 TPS via distributed message-queue technology. The aim is a barebones infrastructure capable of supporting decentralized CDNs while staying adaptable to specific use cases.

Key ideas:
- **Proof of Replication & Geolocation** – inspired by Filecoin; geolocation per https://eprint.iacr.org/2021/697.pdf
- **Two utility tokens** – bandwidth and storage; contributors earn per byte provided
- **Scalability protocol** – built on ZeroMQ messaging; depends on [Malamute](https://github.com/zeromq/malamute) (C)
- **Permissioned blockchain initially** (storage nodes are permissionless); to become permissionless once malicious-actor mitigations exist

## References

- Repo: https://github.com/BHare1985/mosaic
- Docs: https://bhare1985.github.io/mosaic (Doxygen, published on GitHub Release)
- Proof of geolocation paper: https://eprint.iacr.org/2021/697.pdf
- Malamute: https://github.com/zeromq/malamute
- ZeroMQ: https://zeromq.org
- Filecoin (proof of replication inspiration): https://filecoin.io
- Project template: modern CMake, CPM.cmake, Format.cmake, PackageProject.cmake (see README)

## Layout

- `include/`, `source/` – library code
- `standalone/` – executable target (`Mosaic`)
- `test/` – test suite (`MosaicTests`)
- `documentation/` – Doxygen docs
- `all/` – builds every target together
- `cmake/` – CMake helpers

## Commands

```bash
cmake -S standalone -B build/standalone && cmake --build build/standalone
cmake -S test -B build/test && cmake --build build/test && ./build/test/MosaicTests
cmake -S all -B build && cmake --build build      # everything
cmake --build build --target format               # clang-format / cmake-format check: fix-format
```

## Conventions

- C++ with modern CMake; format with clang-format (`.clang-format`) and cmake-format (`.cmake-format`).
- Manage dependencies via CPM.cmake; keep library and executable code separated.
- Add/update tests in `test/` for behavior changes.
