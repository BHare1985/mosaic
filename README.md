# Mosaic

**A brokerless, verifiable, replicated-log library and daemon for C++20, built on ZeroMQ.**

Every writer owns signed, hash-chained, append-only logs per *channel*. Nodes subscribe to channel *patterns*, replicate the
logs they follow straight from each other, and repair anything they missed from any peer that holds it. There is no broker,
no consensus and no coordinator in the data path. Credit-based flow control keeps slow consumers from hurting anyone, and an
optional job layer lets you call services by name over the same logs.

It descends from [Dafka](https://github.com/zeromq/dafka) (brokerless log) and [Malamute](https://github.com/zeromq/malamute)
(channels, services, flow control), implementing the parts of Malamute's design that were only ever on paper. Not wire-compatible
with either.

> **Status: it runs between two machines.** Signed, hash-chained logs; pattern subscriptions; credit-paced repair; a versioned
> binary wire format with permanent message ids and a total decoder; Ed25519 identities; a durable SQLite store that survives a
> torn tail; the ZeroMQ transport with CURVE; and a CLI that publishes and tails across processes - all verified over real TCP.
> Convergence is proven by a seeded sweep under 30% loss, 10% duplication and jitter (`Mosaic sim`).
>
> Still to do before real data: **peer-key pinning needs a ZAP handler** (SEC-1b) - links are encrypted and the server is
> authenticated, but a server cannot yet verify which key a client presented, so identity is pinned only where observable.
> Also open: gossip/beacon discovery, writer-offline head exchange (HEADS-1), retention, and the ACK/min-replica rule.

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
Requires a C++20 compiler and CMake ≥ 3.14 (developed on GCC 15 / CMake 4). `doxygen` is needed only for the
reference documentation; `clang-format` only for the format check (both are reported as skipped if missing).

```bash
# fast loop: core library and tests only
cmake -S test -B build/test -DMOSAIC_WITH_ZMQ=OFF -DMOSAIC_BUILD_ZMQ_FROM_SOURCE=OFF
cmake --build build/test -j && ./build/test/MosaicTests

# everything, including libzmq/czmq/malamute compiled from source (slow; also the ZeroMQ compile check)
cmake -S all -B build && cmake --build build -j && ./build/all/test/MosaicTests
./build/standalone/Mosaic match 'storage.*.shard7' storage.eu.shard7

# the whole check, as CI runs it: four configurations, sanitizers, docs, format
./scripts/ci.sh

# reference documentation (API from the public headers + the design set)
./scripts/docs.sh && open build/all/documentation/doxygen/html/index.html
```

## Running a node

The daemon reads a ZPL config (ZeroMQ RFC 4) and runs until interrupted. `mosaicd --defaults` prints a commented file
generated from the daemon's own defaults, so the documented values cannot drift from the running ones.

```bash
mosaicd --defaults > mosaicd.zpl   # then fill in node.id, node.store, node.keys, peers and follows
mosaicd --check mosaicd.zpl        # every problem at once, before any socket is opened
mosaicd mosaicd.zpl
kill -USR1 <pid>                   # dump the counters now
```

What the counters mean, and what to check when a node is not converging: [docs/design/09-operations.md](docs/design/09-operations.md).

## Two hosts, end to end

Verified over real TCP with CURVE enabled: one process publishes, another tails, and a restarted tail replays its own
durable history before following live traffic.

```bash
# one identity per node (prints the hex id and the CURVE public key)
Mosaic keygen a.key && Mosaic keygen b.key

# on host B: tail everything under events.
Mosaic tail 'events.>' --node b --keys b.key --store b.db \
    --listen 127.0.0.1 --port 6200 --peer "a=127.0.0.1:6100,<A curve key>"

# on host A: publish lines from stdin; each one prints its seq and hash
printf 'first\nsecond\n' | Mosaic pub events.a --node a --keys a.key --store a.db \
    --listen 127.0.0.1 --port 6100 --peer "b=127.0.0.1:6200,<B curve key>"
```

What that demonstrates: signed hash-chained records, a versioned binary wire format, CURVE-encrypted links, durable storage on
both sides, and repair - a tail that was away when a record was published catches up from a HEAD announcement instead of
losing it. Peer endpoints come from configuration only (no discovery service), and a missing peer is not fatal.

## Contributing
Read [docs/design/decisions.md](docs/design/decisions.md), [00-overview](docs/design/00-overview.md) and
[conventions](docs/design/conventions.md), then pick a task from the [TODO board](docs/design/TODO.md).

## Background
[Dafka and Malamute, how they actually work](docs/malamute-and-dafka-system-design.md) ·
[ZeroMQ upstream issues found while reviving the build](docs/upstream-notes.md) ·
[original Mosaic vision (optional future layers)](docs/direction.md#future-layers-the-original-mosaic-vision-kept-for-later)

License: see [LICENSE](LICENSE).
