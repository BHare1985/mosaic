# Proposal

## Why

Two gaps belong together because they meet in the same file. First, the only durable backend is SQLite: it works, it is
crash-safe, and it hides a log inside a relational database - which is fine until a deployment wants to ship, copy, truncate or
inspect a log as bytes on disk. A log is an append-only sequence of signed records; a segment file says exactly that, and STORE-2
is its format. Second, nothing can be forgotten. `min_replicas` decides when a writer may consider a record safe, but a replica
grows forever, and a replica that truncates its log today can no longer verify the next record it receives, because the record's
`prev` hash points at something it no longer has.

A signed checkpoint closes that circle: after truncation, the replica still knows the hash at the truncation point, and the writer
signs it.

## What Changes

- **`SegmentLogStore`**: a durable store that writes one directory per log with fixed-size segment files, a record index and a
  documented header. It passes the shared conformance suite, recovers a torn tail on open, and continues the same log across
  restarts.
- **Retention**: a store can drop a prefix of a log, keeping a signed checkpoint of the last dropped record so the log's
  contiguity requirement is preserved from a new origin.
- **Checkpoints**: a `mosaic/checkpoint` record signed by the writer, carrying `(seq, hash)` of the record it stands for. A replica
  that has truncated its prefix verifies new records against the checkpoint instead of the missing predecessor, and refuses a
  chain that does not descend from it.
- Per-log retention policy in the daemon and CLI config: a record is kept while `min_replicas` is unmet, and may be dropped only
  beyond a configured age or size once it is replicated.

## Capabilities

### New Capabilities

- `store/segment-log-store`: the on-disk format, recovery rules and operational properties of the segment backend.

### Modified Capabilities

- `store/log-store`: retention and truncation become part of the store contract, and a store may answer "what is your origin"
  with a checkpoint rather than a record.
