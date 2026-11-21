# Design

## Context

`LogStore` has `append`, `append_batch`, `head_seq`, `get`, `read`, `keys`, `evidence`. Two implementations: `MemoryLogStore`
(no durability promise) and `SqliteLogStore` (WAL, `Durability::{PerRecord, Batched}`, passes the shared conformance suite in
`test/source/store_conformance.*`). Records are opaque to the store: signature, hash and chain are not the store's business
(`core/record-integrity`, `store/log-store` "the store never verifies").

Measured numbers we already have: durable append 648 µs per record (`PerRecord`) vs 49 µs (`Batched`); `append_batch` 1.9-5.0 µs
per record.

## Decisions

### D1 - Segments, not a database, and not a replacement

`SegmentLogStore` is a third backend, not a rewrite. SQLite stays the default (it is one file, it is battle-tested, it makes
`Mosaic verify` trivial); segments exist for deployments that want the log to be a first-class artefact they can copy, ship and
inspect, and for retention, where dropping a prefix means deleting whole files rather than `DELETE FROM` on a table that then
grows freelist holes.

Choosing this also keeps the conformance suite honest: two very different backends passing the same suite is the strongest
statement the store contract can have.

### D2 - Layout: `<store>/<log-id>/<first-seq>.seg` plus one index per segment

```
store/
  HEAD                       # format byte + version
  6f3a...c1/                 # writer identity, base32
    events.a/
      0000000000.seg         # segment header, then length-prefixed records
      0000000000.idx         # fixed-width entries: offset u32, length u32, hash32
      0000000100.seg
```

Segment size 8 MiB by default: large enough that the index is small and the syscall count is low, small enough that deleting one
is a cheap unit of retention. Records are written length-prefixed (`u32`) with a CRC over the frame; the CRC is what makes "torn
tail" detectable without a second file per record.

The index is derived state. If it disagrees with the segment, the segment wins and the index is rebuilt from the segment on open -
because the segment holds what the writer signed, and the index holds what we guessed. That rule is also why the "damaged
segment" requirement says *report missing*, not *return bytes*.

### D3 - Durability stays a named choice

`SegmentLogStore::open(path, Durability)` mirrors the SQLite backend: `PerRecord` fsyncs the segment before returning from
`append`; `Batched` fsyncs on a flush (size or time), and the documentation states the crash-loss window. No new promise is
invented, and the conformance suite's durability tests parameterise over both.

### D4 - Checkpoints are signed records in a reserved namespace

A checkpoint is a record in the writer's own log on a reserved channel segment: `<channel>/@checkpoint` - no. Simpler and more
honest: the checkpoint is a *separate* small object, signed by the writer, stored next to the log:

```
mosaic/checkpoint payload: u8 format | u8 alg | varint seq | u8 hash[32] | sig[64]
```

It is not a record in the log, because putting it in the log would move the head whenever retention ran, and every follower would
have to replicate retention decisions it did not make. It is signed so a replica can prove to itself (and to `Mosaic verify`) that
hash(seq) is what the writer said it was.

Where it comes from: the writer signs a checkpoint on request (`Mosaic checkpoint <channel>`) and publishes it as a record in the
channel so replicas can store it - the record form is how it travels; the store form is how it is kept. Two forms, one signature.

### D5 - Retention is a policy evaluation, never a timer

`retention()` is called from the daemon's tick and from an explicit CLI command; it evaluates: eligible prefix = records with
`seq <= head - keep_min`, older than `max_age_ms`, in a complete segment, acknowledged by `min_replicas` (when set). It drops whole
segments only, so the checkpoint advance is always to a segment boundary - which keeps the arithmetic simple and the crash story
short (delete a file, then rewrite HEAD; a crash between the two is recovered by checking whether the last checkpoint matches the
files present).

Why never drop a record whose retention is unknown: with `min_replicas` set, "unknown" must mean "not confirmed", and dropping on
a guess silently loses the guarantee ACK-1 exists to provide.

### D6 - Verification across a truncation uses the checkpoint as origin

`ReplicaLog::ingest` verifies `record.prev == hash(previous)`. After truncation, the previous record is gone, so ingest consults
the store's origin: if the record's `prev` equals the checkpoint hash and its `seq` is `checkpoint.seq + 1`, the chain descends
from the checkpoint and is accepted; if the record's `prev` is unknown and no checkpoint covers it, it is refused as before.

Fork evidence is unaffected: a fork is two records at the same sequence with different hashes, and truncation cannot manufacture
one.

## Risks

- **A third backend is a third set of bugs.** Contained by the shared conformance suite and by making segments opt-in.
- **Index rebuild cost on open** after an unclean shutdown: proportional to the segment size, bounded by 8 MiB per segment.
- **Retention is the only operation that destroys data.** It is therefore opt-in per log, reports what it dropped, and refuses
  when any condition is unknown.
- **Checkpoint signing means the writer must be online.** A writer that is gone leaves its replicas unable to truncate - which is
  the correct outcome: nobody else may say what the history at sequence N was.
