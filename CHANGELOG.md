# Changelog

Versions are `0.<closed milestones>.0`; the patch component carries fixes only. **`0.x` minors may change APIs**: the library
is not frozen until the C ABI lands. What each version *must* do is specified in `openspec/specs/`; this file records what
shipped.

## 0.7.0 — 2026-11-21

Seven milestones closed (M0 scaffold through M6 reach), so the minor number moves. Everything below is specified in
`openspec/specs/` and enforced by `./scripts/ci.sh`.

**Security and hardening**

- Peer scoring (SEC-2): weighted strikes for objective faults, decay over time, escalating bans with a ceiling, bans that lapse on
  their own or lift on a good hello plus one verifiable record. A ban is local policy - it never changes what a record is believed
  to say.
- Writer key rotation (SEC-3): a signed link record in the writer's own log names its successor; followers walk a bounded chain
  from a root they already trust, and a channel rule may name that root.
- Transport hardening (ZMQ-2): the poll set is sized from its peers (it was a three-entry stack array), frame counts are enforced,
  the frame limit is a socket option, handover lets a restarted node reclaim its identity, heartbeats notice a silent link - and
  the C ABI and Python binding encrypt by default instead of quietly running plaintext.

**Discovery**

- Peer-list gossip (DISC-3): `PeersMsg` over the existing transport, so gossip inherits CURVE, ZAP, request budgets and scoring.
  Soft-state entries with ages, a hop distance, a bounded table, and the rule that freshness only improves.

**Storage and retention**

- A segment-file backend (STORE-2) whose layout is the log's own shape, with recovery on open and an index that is derived state.
- Checkpoints and retention (STORE-3): a writer signs `(seq, hash)`, a backend keeps it, and only then may a prefix be dropped - in
  whole segments where a backend has them. Verification across a truncation uses the checkpoint as the origin.

**Protocol**

- The hello now carries what a peer follows and one byte of spare capacity, and the protocol version is 2 (HELLO-1). Job workers
  derive their worker set from it and rank saturated peers last (JOBS-4).

**Performance**

- Matcher: `matches_any` allocates nothing in steady state - 4.8 → 0.19 µs at 1k subscriptions, 179.7 → 4.2 µs at 100k (MATCH-4).
- Batched ingest (LOG-2): a repair reply of 256 records costs one store call, ~2.1 µs per record.

**Process**

- The style backlog reached zero (STYLE-2) and the gate closed behind it: `clang-tidy` now fails the build on any finding,
  correctness or style. `MOSAIC_LINT_STYLE` is gone.

## 0.6.0 — 2026-11-17

Six milestones closed (M0 scaffold through M5 jobs). The first version that describes the system rather than the template it
started from.

**The log layer**

- Signed, hash-chained, single-writer logs per channel; Ed25519 signatures over an algorithm-tagged canonical encoding, so a log
  written under one hash still verifies after the default changes.
- A versioned binary wire format: permanent message ids, length-framed, a total decoder that treats input as hostile, golden
  vectors and a mutation fuzz test.
- Equivocation is kept, not just noticed: a replica stores both records of a fork and `Mosaic verify` re-checks a whole store
  offline.

**Replication**

- ZeroMQ transport: PUB/SUB for live traffic, ROUTER/DEALER for repair, CURVE on every link, peer keys decided at the handshake
  by a ZAP handler (open with per-address pinning, or closed against a provisioned list).
- Repair served by any holder, credit-paced; followers converge under loss, duplication, delay, reordering and partitions, proven
  across seed sweeps rather than one run.
- A writer can go offline: replicas acknowledge what they hold, retention is governed by `min_replicas`, newcomers learn heads from
  peers, and a follower too far behind switches to repair instead of drowning.

**Storage**

- Memory and SQLite stores behind one seam, validated by a shared conformance suite. Durability is a named choice: per-record
  commit (default) or batched, with the crash-loss guarantee stated and measured.

**Optional layers**

- Jobs: call a service by name over logs - deterministic rendezvous ranking, ranked retry when a worker dies, first result wins.
- Discovery: LAN peers found by a cluster-scoped beacon, with evasive/expired peer tracking. Gossip is not implemented
  (czmq's event stream is private); tracked as DISC-3.
- Per-channel write authorization, so `svc.*` can be closed to anyone but its named writers.

**Reach: C and Python**

- A C ABI: `include/mosaic/mosaic_c.h`, one header of opaque handles, value structs and status codes, with the records passed
  to your callback borrowed and copyable. It adds no behaviour, and the Python binding is written against it alone.
- `bindings/python`: a compiled extension plus a record iterator, built by `scripts/build_python.sh` in one command.
- Proved from outside C++: a C test compiled by a C compiler, the same ABI as two processes (one publishing, one tailing), and a
  Python process tailing records published by another Python process.

**Operations**

- `mosaicd` with a ZPL configuration read by czmq's `zconfig`: every setting in one file, validated whole before anything starts,
  defaults printable and authoritative, counters on `SIGUSR1`, and a clean shutdown.
- `Mosaic pub`, `tail`, `verify`, `keygen`, `sim`.

**Groundwork**

- A written C++ style enforced by `.clang-format` and `.clang-tidy`; correctness is gated in CI, style suggestions are counted.
- One documentation folder with a published reference site built from the public headers.
- A CI script (four build configurations, sanitizers, documentation, lint, format) that runs the same way locally and in CI.

**Not provided:** global ordering, consensus, consumer groups, exactly-once execution, retention with checkpoints, gossip
discovery.
