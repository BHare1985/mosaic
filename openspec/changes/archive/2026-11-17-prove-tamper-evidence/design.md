# Design

## Context

See proposal.md. `ReplicaLog::ingest` already computes every outcome including `Fork`; the store holds records and nothing else;
`Node` owns the fork callback; CI does not exist and the platform is a single Linux machine with four working build
configurations (sodium on/off x ZeroMQ on/off).

## Goals / Non-Goals

**Goals:** evidence that survives a restart and is bounded; a verify command an operator can trust; CI that runs the matrix
instead of asking a human to remember it.

**Non-Goals:** gossiping evidence, punishing a fork, membership consequences, verifying a store while a node writes to it (the
command reads a store, it does not coordinate with a live writer).

## Decisions

**D1 - Evidence lives in the log layer, not the store interface.**
`ReplicaLog` gains a bounded `forks()` collection (log, sequence, both records). The store keeps being a dumb record container;
requiring every backend to implement evidence would multiply the conformance surface for a rare event.
*Alternative:* a second table in the SQLite store - rejected: it makes evidence a per-backend feature.

**D2 - Evidence is bounded and counted.**
A writer that equivocates at every sequence could otherwise grow a replica's memory without limit. The cap is configuration; the
overflow increments a counter so the condition stays visible.

**D3 - `verify` reads records through `LogStore` and re-derives everything.**
It reconstructs the hash of each record from its canonical bytes, checks each signature against the writer identity, and checks
each `prev` link - the same rules as ingest, applied statically. A gap or an undecodable record is a failure, not a skip:
"verified 10,000 records, 1 unreadable" that exits zero would be a lie.

**D4 - CI covers the matrix that already exists by hand.**
Four build configurations (sodium on/off, ZeroMQ on/off), plus a sanitizer build, plus the weekly ecosystem compile check. The
point is not maximum coverage; it is that the greens stop depending on one machine.

## Risks / Trade-offs

- **Evidence of a fork could itself be forged** -> a replica only stores a fork it verified: both records must be validly signed
  by the same writer, so the evidence is as trustworthy as the signature.
- **`verify` is slow on large stores** -> it is a batch tool; it reads sequentially and prints progress counts rather than
  loading everything.
- **CI will find things** -> that is the point, and it is why it goes first.

## Open Questions

- Whether evidence should be surfaced over the protocol (a `ForksMsg` or a gossip of evidence records). Deferrable: keeping it
  locally is the prerequisite.
