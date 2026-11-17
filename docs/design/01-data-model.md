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

## Open design questions (resolve before the matching TODO)
- **Retention/truncation (STORE-3):** replicas may drop old records only if they keep a signed checkpoint
  `(seq, hash)`; replaying from a checkpoint must still verify. Define the checkpoint record type.
- **Fork evidence (LOG-1):** store both conflicting records; define how it is gossiped.
- **Writer key rotation:** start a new log and write a link record in the old one (SEC-3).
- **Durable writer identity:** writer id is the public key (not a random UUID like Dafka), so a restart keeps its log.
