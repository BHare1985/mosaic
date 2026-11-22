# Proposal

## Why

`bound-peer-requests` put a ceiling on what one peer can ask for: request budgets per window, a head-pattern limit, bounded
pending jobs. What it did not do is make a peer *pay* for being wrong. A peer that sends undecodable frames, records whose
signatures fail, or oversized records is counted (`dropped`, `bad_signature`, `oversized_records`) and then served again at
full rate on the next tick. Counting makes abuse visible; it does not make it expensive. SEC-2 is the missing half.

## What Changes

- Every peer accumulates strikes for objective faults: an undecodable message, a record whose signature or chain does not
  verify, a record over `max_record_bytes`, an unrequested record, a hello speaking another protocol version.
- Strikes are weighted by kind and decay over time, so a single bad frame from a good peer costs nothing lasting while a flood
  escalates.
- Crossing a threshold bans the peer for a duration that doubles with repeated crossings, up to a ceiling. A banned peer's
  messages are dropped before decoding, and it is not eligible for repair or for job ranking.
- Bans are local policy only: they never change what the data layer accepts, so a ban cannot make a node accept a bad record or
  reject a good one.
- `NodeCounters` gains `bans_started`, `peer_bans`, and `strikes_by_kind`; `mosaicd` reports bans on `SIGUSR1` and logs one line
  when a ban starts and when it expires.

## Capabilities

### New Capabilities

- `security/peer-scoring`: how a node decides that a peer is misbehaving, how long that decision lasts, and what it must not
  affect.
