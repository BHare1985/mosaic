# Direction

> **Status 2026-11-21:** direction **A** was carried out. Everything in the recommended direction is implemented and specified in
> [`openspec/specs/`](../openspec/specs); the decisions are in [design/decisions.md](design/decisions.md). The only part of this
> document that is still a plan is the optional future layer at the end (parity symbols, tokens, proof of geolocation), which was
> always explicitly optional.
>
> The text below is kept as it was written (2026-11-09) - it is the original analysis, not a description of the current code. Where
> it and the specifications differ, the specifications win.

## Where we are

Mosaic began (2024) as a "decentralized data parity broker": Malamute for
messaging, Dafka's ideas for a brokerless log, and a blockchain-like storage
layer on top (fountain-code parity, storage/bandwidth tokens, proof of
replication). The repo today is a CMake template with a "hello world" class
plus a Malamute send/receive test. The build works again on GCC 15 / CMake 4
(see [upstream-notes.md](upstream-notes.md)).

Observations from reading the references:

* **Malamute is a broker.** Its mailbox / service / stream patterns are good
  but centralized: one server process, clients connect to it. That conflicts
  with "decentralized" and is the wrong substrate for the final design.
  It is still valuable as a *reference for patterns* (credit-based flow
  control, subject patterns, protocol-as-XML codegen) and as a *compile check*
  for the ZeroMQ stack.
* **Dafka is the right shape and is abandoned.** Per-producer partitions,
  direct producer→consumer delivery, stores as passive replicas, consumer-owned
  offsets, no consensus. Last commit 2020; depends on LevelDB and czmq draft
  APIs; spec is RFC 46. Its core insight (the log is single-writer, so you
  never need consensus to *replicate* it) is exactly the primitive a
  blockchain-like system needs underneath.
* **Tokens, proof-of-replication and 100k TPS are layers 3+.** They need a
  trustworthy replicated log first. Starting there is why the project stalled.

## Recommended direction

> **Mosaic = a small, modern, brokerless replicated-log library and daemon,
> built on libzmq, that is useful on its own.** Blockchain/storage features
> become optional layers on top, later or never.

Concretely:

1. **Core:** signed, hash-chained, single-writer append-only logs, replicated
   peer-to-peer with Dafka's message set (RECORD / HEAD / FETCH / ACK ...).
   Tamper-evident by construction; no consensus needed to replicate.
2. **Pluggable pieces** (each one small interface): storage (SQLite/LMDB/flat
   segments), discovery (static list, gossip, mDNS), transport security
   (CURVE + identity keys).
3. **Ship as:** a C++20 library with a stable C ABI, a `mosaicd` daemon,
   and a CLI (`mosaic pub|sub|tail|verify`). Language bindings follow the C ABI.
4. **Parallel track, "ZeroMQ still builds":** a CI matrix that builds libzmq,
   czmq, malamute and dafka from pinned refs on current compilers. This is
   cheap, directly serves the revival goal, and is where upstream fixes get
   found (always discussed with you first).

Why this is useful in 2026 (candidate use cases, in my order of confidence):

* **Local-first / offline-first sync** between devices or services that cannot
  reach a central Kafka, with verifiable history.
* **Edge / IoT / on-prem event streams** where running ZooKeeper-or-KRaft
  class infrastructure is too heavy.
* **Tamper-evident audit logs** across organizations (each party owns its log).
* **Event bus for multi-agent / automation systems** where each agent is a
  single writer with its own identity and logs must be replayable.
  (Plausible fit, not validated.)

## Future layers (the original Mosaic vision, kept for later)
Fountain-code parity symbols, proof of replication/geolocation (Filecoin-inspired), bandwidth/storage utility tokens and a
permissioned chain are **not** part of the core. If pursued, they consume these logs as transport and evidence store.
Original notes: https://eprint.iacr.org/2021/697.pdf (geolocation).

## Alternatives considered

| Option | For | Against |
|---|---|---|
| A. Dafka-style brokerless log (recommended) | Matches original intent, decentralized, small core, lets chain layers come later | Must re-implement; Dafka code is old, so we borrow the *design*, not necessarily the code |
| B. Revive/fork Dafka as-is | Fastest to a working demo | Inherits LevelDB, czmq-draft and 2020-era code; C-only; hard to evolve |
| C. Build on Malamute | Working today, already compiles | Centralized; contradicts the goal |
| D. Compatibility/CI project only | Clear ecosystem value, low risk | Not a product by itself |
| E. Keep the original chain/token vision | Original idea | Biggest scope, no usable layer below it yet |

A + D together is the recommendation; B is a fallback if you would rather
start from working Dafka code.

## Decisions needed from you

1. **Direction:** A (+D), B, or something else?
2. **Language/ABI:** C++20 core with C ABI (my suggestion), or keep it plain C
   in the CZMQ/CLASS style the ZeroMQ projects use?
3. **Dafka compatibility:** wire-compatible with RFC 46, or "inspired by",
   free to change the protocol (my suggestion: inspired by, versioned from day 1)?
4. **Upstream:** I have not filed or patched anything. Which of the items in
   [upstream-notes.md](upstream-notes.md) do you want to take upstream, and
   should they come from your account (you made PRs in 2024)?
5. **Name and README:** keep "Mosaic" and drop the "parity broker / token"
   framing from the README for now?
6. **Local build workarounds:** the malamute `distclean` patch is applied at
   fetch time in this repo only. Fine to keep until upstream moves?
