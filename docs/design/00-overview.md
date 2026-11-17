# 00 — Overview

_Describes 0.6.0._

## Goal
A small, dependency-light library (C++20) for **brokerless, verifiable, replicated logs** with
**channel-based subscription**, **credit-based flow control** and an optional **job/service layer**, designed
to run over ZeroMQ. Built from [Dafka](https://github.com/zeromq/dafka)'s log (✔ durable, replayable, no consensus, see [RFC 46](https://rfc.zeromq.org/spec/46)) plus
[Malamute](https://github.com/zeromq/malamute)'s ideas that were
never implemented (✔ fast matching, ✔ credits, ✔ services). See [../direction.md](../direction.md).

**Non-goals (for now):** global ordering, consensus, consumer groups, exactly-once, tokens/blockchain. Those are
layers that consume logs; none may be required to use a log.

## Architecture

```
                 app code
    ┌───────────────────────────────┐
    │  jobs      (rendezvous ranking, request/result over logs)   ⬜/🟡
    ├───────────────────────────────┤
    │  node      Node: writer + replica + responder                ✅
    ├────────────┬─────────┬────────┤
    │  net       │  flow   │ match  │  Transport / SimNetwork ✅, ZMQ ⬜ │ credits ✅ │ Matcher ✅
    ├────────────┴─────────┴────────┤
    │  proto     messages (values; binary encoding ⬜)             🟡
    ├───────────────────────────────┤
    │  log       LogWriter, ReplicaLog (verification)              ✅
    ├───────────────────────────────┤
    │  store     LogStore interface, MemoryLogStore; SQLite ⬜      🟡
    ├───────────────────────────────┤
    │  core      types, Record, canonical bytes, crypto seams      ✅ (real Ed25519 ⬜)
    └───────────────────────────────┘
```

**Threads.** Everything in `core`..`node` is single-threaded and lock-free; that is what makes a simulation deterministic and a
failure reproducible. The one exception is a czmq actor (today: `zbeacon`, inside `MosaicDaemon`): it owns its own thread, and
everything it learns crosses into the node loop as a *value* on its actor pipe, which the loop drains non-blockingly. No state is
shared, so no lock exists. Any future thread must follow the same rule or not exist.

### Dependency rules (enforce in review)
**Third-party code — three dependencies, each confined.** `libzmq` behind `Transport` (the `MosaicZmq` target); `czmq` behind
the daemon and CLI only (ZPL config today, discovery next), never in `core`..`node` and never in the `core` / `no-crypto`
builds; and `libsodium` behind `Hasher`/`Signer`/`Verifier`. `libsodium` behind `Hasher`/`Signer`/`Verifier` (fetched
by CPM, optional via `MOSAIC_WITH_SODIUM`) and `libzmq` behind `Transport`, confined to the separate `MosaicZmq` target. A
consumer that packages Mosaic must supply `libzmq` and, if `MOSAIC_WITH_SODIUM` was used, `libsodium`: they are linked
`PRIVATE $<BUILD_INTERFACE:...>` today, so they are deliberately absent from the install export. `MOSAIC_WITH_SODIUM=OFF` and
`-DMOSAIC_WITH_ZMQ=OFF` together produce a dependency-free core.

`core` ← `store` ← `log` ← `proto` ← `net` ← `node` ← `jobs`. `match` and `flow` have **no** dependencies and may be used
by anyone. A module may include only modules to its left. ZeroMQ headers may appear only inside the future ZeroMQ
transport, confined to the `MosaicZmq` target. The core library and its tests must keep building with
`-DMOSAIC_WITH_ZMQ=OFF -DMOSAIC_BUILD_ZMQ_FROM_SOURCE=OFF`.

### Module map

| Module | Header dir | Key types | README |
|---|---|---|---|
| core | `include/mosaic/core` | `Record`, `LogKey`, `Hash`, `Hasher`, `Signer`, `Verifier` | [link](../../include/mosaic/core/README.md) |
| store | `.../store` | `LogStore`, `MemoryLogStore` | [link](../../include/mosaic/store/README.md) |
| log | `.../log` | `LogWriter`, `ReplicaLog`, `IngestResult` | [link](../../include/mosaic/log/README.md) |
| match | `.../match` | `Matcher` | [link](../../include/mosaic/match/README.md) |
| flow | `.../flow` | `CreditWindow`, `CreditGranter` | [link](../../include/mosaic/flow/README.md) |
| proto | `.../proto` | `Message` and its alternatives | [link](../../include/mosaic/proto/README.md) |
| net | `.../net` | `Transport`, `SimNetwork`, `Discovery` | [link](../../include/mosaic/net/README.md) |
| node | `.../node` | `Node`, `NodeConfig` | [link](../../include/mosaic/node/README.md) |
| jobs | `.../jobs` | `rendezvous_rank`, `JobRequest`, `JobResult` | [link](../../include/mosaic/jobs/README.md) |

## One record's journey (how the blocks fit)

1. App calls `Node::publish(signer, "events.a", type, payload, now)`.
2. `LogWriter` assigns `seq`, sets `prev` = hash of the previous record, signs → `LogStore::append`.
3. Node calls `Transport::publish("events.a", RecordMsg)`. The transport delivers it **only** to peers whose
   subscription patterns (`Matcher`) match `events.a`.
4. A follower's `Node::pump` receives it, checks `following_.matches_any(channel)`, and calls
   `ReplicaLog::ingest`: signature ✔, chain ✔ → `Accepted` → stored → `on_record` callback.
5. If the follower lost an earlier record, ingest returns `Gap`. Node sends `FetchMsg{from, count}` to the sender.
   `count` is limited by that peer's `CreditWindow`. The responder (any node holding the log) replies with
   `DirectRecordMsg`s; each returns one credit.
6. Independently, the writer's `tick` publishes `HeadMsg` every `head_interval_ms`; a follower that is behind
   fetches. This repairs tail loss when no later record arrives to reveal a gap.
7. If a record fails verification it is dropped; if it contradicts a stored record, `on_fork` fires.

## Glossary
- **Writer**: holder of a signing key; owns a log per channel. **Log**: `(writer, channel)` append-only chain.
- **Follower/replica**: a node storing someone's log. **Channel**: dotted name. **Pattern**: subscription with `*`/`>`.
- **Head**: `(seq, hash)` of the last record. **Gap**: missing seq range. **Fork/equivocation**: two valid records at one seq.
- **Credit**: permission to send N more units.
