# Design

## Context

See proposal.md. The two algorithmic problems are visible in `Node::acknowledge` and `Node::advance_retention`; the measured
append cost is in the store README (818us, 5000 appends); the matcher is a bitset design whose claim has never been checked.
Everything is single-threaded and clock-injected, so benchmarks can drive ticks deterministically.

## Goals / Non-Goals

**Goals:** know the costs, remove work that scales with data volume, and give durability a faster mode without weakening the
default.

**Non-Goals:** micro-optimising the hot loop before measuring it, changing the matcher's semantics, or a new backend.

## Decisions

**D1 - Benchmarks live in the test binary, with generous bounds.**
A benchmark that fails CI on a noisy machine is worse than none: the asserted bound is an order of magnitude, not a percentage,
and the *number* is printed for humans. *Alternative:* a separate benchmark harness - rejected: it would not run in CI and would
rot.

**D2 - Acknowledgements are driven by ingest, not by inventory.**
A node remembers which logs it recently accepted records for (already the shape of `source_peer_`) and acknowledges those. The
store's `keys()` is a reporting API, not something a loop should call.

**D3 - The retention floor is the k-th largest acknowledgement.**
Collect the acks for a log, take the `min_replicas`-th largest: that is the sequence every one of them has confirmed. It is
O(r log r) with r replicas, obviously correct, and it removes a loop whose termination condition depended on a `break`.

**D4 - Durability modes are named, not a boolean.**
`per_record` (default, `synchronous=FULL`) and `batched` (group commit, or `synchronous=NORMAL` where group commit is not
available). The docs state what a crash can lose in each mode, because "fast" is not a specification.

**D5 - Batch append is one call into the store, not a loop in the node.**
`append(span<const Record>)` so a SQLite backend can wrap the range in one transaction. The conformance suite covers it by
requiring the batch to behave exactly like N single appends.

## Risks / Trade-offs

- **Benchmarks flake on shared CI** -> order-of-magnitude bounds, printed numbers, and the strict assertions limited to the
  scaling property (work independent of volume) rather than wall time.
- **Batched durability loses records on crash** -> it is opt-in, documented, and the default is unchanged; the store
  conformance suite runs in both modes.
- **The retention rewrite could change behaviour** -> the existing shortfall and retention tests are the specification; they run
  unchanged against the new implementation.

## Open Questions

- Whether group commit should be time- or size-bounded. Deferrable: `synchronous=NORMAL` is the first step and needs no new state.
