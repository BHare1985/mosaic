# Changelog

Versions are `0.<closed milestones>.0`; the patch component carries fixes only. **`0.x` minors may change APIs**: the library
is not frozen until the C ABI lands. What each version *must* do is specified in `openspec/specs/`; this file records what
shipped.

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

**Operations**

- `mosaicd` with a ZPL configuration read by czmq's `zconfig`: every setting in one file, validated whole before anything starts,
  defaults printable and authoritative, counters on `SIGUSR1`, and a clean shutdown.
- `Mosaic pub`, `tail`, `verify`, `keygen`, `sim`.

**Groundwork**

- A written C++ style enforced by `.clang-format` and `.clang-tidy`; correctness is gated in CI, style suggestions are counted.
- One documentation folder with a published reference site built from the public headers.
- A CI script (four build configurations, sanitizers, documentation, lint, format) that runs the same way locally and in CI.

**Not provided:** global ordering, consensus, consumer groups, exactly-once execution, retention with checkpoints, gossip
discovery, a C ABI.
