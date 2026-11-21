# 01 — Data model (✅ implemented, in-memory)

_Describes 0.6.0._

## Record
```
Record { writer, channel, seq, prev, timestamp_ms, content_type, payload, sig }
hash = H(hash_algo)(canonical_bytes(record))      // sig excluded; H named by the record's own tag
sig  = Sign(writer_key, hash)
prev = hash of record seq-1 (all-zero when seq == 0)
```
`canonical_bytes` (frozen, format version 1): `u8 1 | u8 hash_algo | writer | channel | u64 seq | 32B prev |
u64 timestamp_ms | content_type | payload`, integers big-endian, each variable field prefixed by a `u32` length. Length
prefixes make the encoding unambiguous, and a decoder rejects any encoding that does not re-encode identically (both tested).
`hash_algo` is `1` for SHA-256 and `2` for BLAKE2b; it is inside the hashed bytes so a verifier reads the algorithm from the
record instead of from a build setting, and a log written under one algorithm stays verifiable after the default changes.
`timestamp_ms` is the writer's claim, not trusted for ordering.

**When building a record by hand** (tests, tools, future ACK/jobs work) you must stamp `hash_algo` yourself: `LogWriter` does
it from its hasher, but a struct literal defaults to the build's default algorithm and will then hash under the wrong function.

A **log** is `(writer, channel)`. Ordering exists **only inside a log**. There is no cross-log order.

## Why a hash chain
Anyone holding the head hash can verify the whole prefix; a replica can detect that a writer showed different
histories to different peers (**fork / equivocation**): two validly signed records with the same `(writer, channel, seq)`
but different hashes, or a record whose `prev` disagrees with what we stored.

## Verification rules (`ReplicaLog::ingest`, in this order)
1. `Verify(writer, hash, sig)` fails → `BadSignature`.
2. `seq < next`: identical hash → `Duplicate`; otherwise → `Fork`.
3. `seq > next` → `Gap` (record is **not** stored; the caller fetches the missing range).
4. `seq == 0` and `prev != 0` → `BadChain`.
5. `seq > 0` and `prev != hash(stored seq-1)` → `Fork`.
6. Otherwise store → `Accepted`.

## LogStore contract (`store/log_store.h`)
Append-only per log, `seq == head+1`; `read(from, count)` returns ≤ count in order; unknown log = empty. The store never
hashes or verifies. Durability is per backend (memory: none; SQLite: WAL commit before returning `Ok`).

## The segment layout (✅ STORE-2)

`SegmentLogStore` writes a log as the thing it claims to be: a directory of append-only files. A reader that knows only this
section can recover every record.

```
<store>/HEAD                             "MOSAICSEG" + u8 format version (1)
<store>/<writer>/<channel>/0000000000.seg    one file per segment, named by its first sequence
<store>/<writer>/<channel>/0000000000.idx    derived index: (u32 offset | u32 length | hash[32]) per record
<store>/<writer>/<channel>/checkpoint.bin    the log's newest checkpoint, if it has one
```

A channel with characters outside `[A-Za-z0-9._-]` is escaped as `%XX`, so `events.a` is its own directory name.

Each record is one frame:

| Field | Width | Note |
|---|---|---|
| length | u32 | count of everything after this field: crc + seq + payload |
| crc | u32 | CRC-32 over seq + payload, so a torn write is detectable without a second file |
| seq | u64 | the record's sequence number |
| payload | length - 12 | the record's wire bytes (canonical bytes plus signature) |

**Recovery is part of opening.** The store scans every segment on open, keeps the frames that validate, and truncates the file at
the first one that does not - a frame that promises more bytes than are there, or whose CRC disagrees. The head is then the last
complete record, and appending continues at head+1. Nothing is guessed: a frame that fails to validate ends the log there rather
than being repaired.

**The index is derived state, never authority.** It is rebuilt from the segment on open and rewritten as records are appended. A
read whose index entry points past the end of the file reports the record *missing*; it never returns adjacent bytes.

`Durability::PerRecord` fsyncs before an append returns. `Batched` reaches the operating system without an fsync: it survives
process death and not power loss, and the documentation says so rather than implying the stronger promise.

## Checkpoints and retention (✅ STORE-3)

A checkpoint is the writer's signed statement about its own history: `(log, seq, hash)`, signed under the algorithm it names, in
a canonical encoding of its own (`u8 format | u8 algorithm | string writer | string channel | u64 seq | hash[32]` plus a
length-prefixed signature). It is **not a record in the log**: putting it in the log would move the head every time retention ran
and force every follower to replicate a retention decision it did not make. When it has to travel it travels as a record on the
same channel; the store keeps the object.

Retention evaluates a *policy*, never a schedule:

1. the prefix is old enough (`retention_max_age_ms`, measured against the record's own timestamp) and beyond the floor
   (`retention_keep_min` records always survive);
2. when `min_replicas` is set, the ACK floor covers the whole prefix - a record whose replication state is unknown is kept,
   because dropping on a guess is exactly the guarantee ACK-1 exists to provide;
3. the writer signs a checkpoint naming the last record being dropped, and the backend stores it before it truncates. A backend
   that cannot keep a checkpoint refuses to truncate, and the refusal is counted rather than hidden.

Truncation drops **whole segments** where a backend has them, so the advance always lands on a segment boundary and the crash
story is short: write the checkpoint, delete files, and on reopen believe the files.

### Verifying a record whose predecessor is gone

`ReplicaLog::ingest` verifies `record.prev == hash(previous)`. After truncation the previous record is missing, so ingest asks the
checkpoint: if `record.prev` equals the checkpoint's hash and `record.seq == checkpoint.seq + 1`, the record descends from the
checkpoint and is accepted. If no checkpoint covers it, the record is refused exactly as before. Fork detection is unaffected:
equivocation is two records at one sequence, and truncation only ever removes a prefix.

## Open design questions (resolve before the matching TODO)
- **Retention/truncation (STORE-3):** replicas may drop old records only if they keep a signed checkpoint
  `(seq, hash)`; replaying from a checkpoint must still verify. Define the checkpoint record type.
- **Fork evidence (LOG-1):** store both conflicting records; define how it is gossiped.
- **Writer key rotation:** start a new log and write a link record in the old one (SEC-3).
- **Durable writer identity:** writer id is the public key (not a random UUID like Dafka), so a restart keeps its log.
