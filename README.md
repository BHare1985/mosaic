# Mosaic

**A brokerless, verifiable, replicated-log library and daemon for C++20, built on ZeroMQ.**

Every writer owns signed, hash-chained, append-only logs per *channel*. Nodes subscribe to channel *patterns*, replicate the
logs they follow straight from each other, and repair anything they missed from any peer that holds it. There is no broker,
no consensus and no coordinator in the data path. Credit-based flow control keeps slow consumers from hurting anyone, and an
optional job layer lets you call services by name over the same logs.

It descends from [Dafka](https://github.com/zeromq/dafka) (brokerless log) and [Malamute](https://github.com/zeromq/malamute)
(channels, services, flow control), implementing the parts of Malamute's design that were only ever on paper. Not wire-compatible
with either.

> **Status: early scaffold.** The core is implemented and tested in an in-process simulator (including convergence under 30%
> packet loss). The ZeroMQ transport, binary wire format, real Ed25519 signatures and persistent storage are still to do.
> `InsecureTestSigner` provides **no security**. Do not use for real data yet.

## Why
Local-first sync, edge/on-prem event streams, tamper-evident audit logs, and job/agent buses all want a durable, replayable
stream without running a broker cluster. A single-writer log needs no consensus to replicate; signatures and hash chains let
anyone verify it. See [docs/direction.md](docs/direction.md).

## How it fits together
```
jobs   rendezvous-ranked service calls over logs
node   writer + replica + responder in one single-threaded, clock-injected state machine
net    Transport (ZeroMQ in production, SimNetwork in tests), Discovery
flow   CreditWindow / CreditGranter          match   bitmap Matcher for channel patterns (*, >)
proto  messages                              log     LogWriter, ReplicaLog (verification)
store  LogStore (memory now, SQLite next)    core    Record, hashing, signing seams
```
Details: [docs/design](docs/design/README.md).

## Build and test
Requires a C++20 compiler and CMake ≥ 3.14 (developed on GCC 15 / CMake 4).

```bash
# fast loop: core library and tests only
cmake -S test -B build/test -DMOSAIC_BUILD_ZMQ_STACK=OFF
cmake --build build/test -j && ./build/test/MosaicTests

# everything, including libzmq/czmq/malamute compiled from source (slow; also the ZeroMQ compile check)
cmake -S all -B build && cmake --build build -j && ./build/test/MosaicTests
./build/standalone/Mosaic match 'storage.*.shard7' storage.eu.shard7
```

## Contributing
Read [docs/design/decisions.md](docs/design/decisions.md), [00-overview](docs/design/00-overview.md) and
[conventions](docs/design/conventions.md), then pick a task from the [TODO board](docs/design/TODO.md).

## Background
[Dafka and Malamute, how they actually work](docs/malamute-and-dafka-system-design.md) ·
[ZeroMQ upstream issues found while reviving the build](docs/upstream-notes.md) ·
[original Mosaic vision (optional future layers)](docs/direction.md#future-layers-the-original-mosaic-vision-kept-for-later)

License: see [LICENSE](LICENSE).
