# Design

## Context

Records carry `writer` (a public key), `channel`, `seq`, `prev` and a signature. `ReplicaLog::ingest` verifies the signature
against `record.writer` and the chain against `prev`, and `LogKey{writer, channel}` is the log's identity in the store.
`ChannelRules` matches `(channel pattern, writer identity)`. A writer key is never assumed to be unique per channel forever, but
nothing today expresses a successor.

## Decisions

### D1 - The link is a record, not a new message

A rotation must survive restarts, replication and repair, and must be provable offline by `Mosaic verify`. All three come free if
the link is an ordinary record: it is in the log, hash-chained, signed, replicated by the same code that replicates records, and
covered by fork evidence. A `RotateMsg` on the wire would need its own durability story and could be lost, leaving two peers
disagreeing about whether a rotation happened.

Content type `mosaic/rotation` with a fixed payload:

```
u8 format | u8 alg | u8 successor_pubkey[32] | varint successor_start_seq
```

The payload is opaque to the log layer, which is what makes an old replica forward-compatible: it stores and forwards the link
without interpreting it.

### D2 - Trust is a local chain walk, not a global registry

`RotationChain` is a small component that answers one question: `resolve(trusted_root, channel) -> vector<Identity>`. It walks
the store: read the log for `(root, channel)`, find `mosaic/rotation` records, parse, verify signature (already done at ingest,
re-verified here because resolution may run before the store was replayed), continue from the successor, stop at the depth limit
or a repeated identity.

Bounded because an unbounded walk is a denial-of-service: a hostile chain can be arbitrarily long, and the walk happens on the
ingest path when rules are consulted. Depth 4 by default: enough for a real rotation cadence, small enough to reason about.

Refusing cycles is not paranoia; a chain A→B→A would make two logs the same log, which is exactly the ambiguity fork evidence
exists to prevent.

### D3 - Rules name a root, and the chain answers the rest

`ChannelRules::allow(pattern, writers)` today stores identities. The delta is: an entry may be marked as a rotation root, and
authorization asks the chain for the current successor set of each root. The set is cached per (root, channel) and invalidated
when a new rotation link is ingested, so the rules check is not a store read per record.

The asymmetry in the spec ("a chain authorizes the successor, never the predecessor") matters: if a rule allows only B and A
rotates to B, A must stop being authorized, otherwise retiring a key by rotating away from it would not retire anything.

### D4 - Rotation is opt-in and per-channel

A writer rotates on one channel; other channels keep the old key. That keeps the mechanism honest about the actual unit of trust
(the log), and means a partially-rotated deployment is a normal state, not a broken one. `Mosaic rotate` therefore takes a
channel and a successor key file, writes the link with the old key, and prints the new `LogKey`.

### D5 - Nothing about verification changes for existing logs

`ingest` still verifies against `record.writer`. Rotation affects only *authorization* and *attribution helpers*; the data layer
learns nothing. That keeps the invariant from `core/record-integrity` untouched: a record's writer is whoever signed it, always.

## Risks

- **A leaked old key can rotate to an attacker's key.** Rotation cannot prevent this, and pretending otherwise would be worse
  than saying it: the mitigating control is that rotation is visible in the log, replicated everywhere, and printed by the CLI, so
  an unexpected link is discoverable.
- **Cache invalidation**: the successor set cache must be cleared on ingest of a link and on store replay. Tested directly.
- **Depth exhaustion in long-lived deployments**: four rotations is a lot; if a deployment exceeds it, the limit is configuration,
  and exceeding it fails closed.
