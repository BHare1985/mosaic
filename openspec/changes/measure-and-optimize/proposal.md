# Proposal

## Why

Three costs are known from reading the code and one is already measured, and none of them is visible to a user until something is
slow:

1. **`acknowledge()` scans the store's inventory every tick.** On SQLite that is a `SELECT DISTINCT writer, channel ... ORDER BY`
   over the whole records table, executed every control interval - a full table scan per tick, whether or not anything changed.
2. **The retention floor is computed by looping.** `advance_retention` walks every sequence from the last floor to the head and,
   for each one, counts acknowledgements across all replicas: O(range x replicas) for what is really "the `min_replicas`-th
   largest ack".
3. **Durable appends cost 818us each** (`synchronous=FULL`, one transaction per record - measured and recorded in the store
   README). Right for an audit log, wrong for an ingest firehose, and there is no option in between.
4. **The matcher's headline claim is unverified.** "Microseconds per match" has been asserted since the design set and never
   measured (MATCH-1). A log library whose cost story is a guess cannot be defended.

This change makes the cost of the system measured and bounded, and fixes the two algorithmic problems that measurement makes
obvious.

## What Changes

- Benchmarks that are part of the suite, not a side project: matcher throughput at 1k/10k/100k subscriptions, per-tick node work
  as a function of store size, and append throughput per store backend.
- Work per tick becomes independent of how much is stored: acknowledgements are driven by recently ingested logs, not by a
  store inventory scan.
- The retention floor is computed as the k-th largest acknowledgement instead of by looping over sequences.
- An opt-in relaxable durability mode for the SQLite store (`synchronous=NORMAL` or group commit), with the trade written down
  where an operator will read it.
- Batch ingest (`LogStore::append` for a span) so repair of a large range costs one store round trip, not N.
- **Not** in scope: a new store backend (STORE-2), cross-process performance work, or changing the wire format.

## Capabilities

### New Capabilities

- `performance/measured-cost`: the costs the library promises to know, the conditions they are measured under, and the bounds that
  keep per-tick work independent of data volume.

## Impact

- `source/node/node.cpp`: ack tracking driven by ingest, retention floor via k-th largest.
- `include/mosaic/store/log_store.h`, `source/store/sqlite_log_store.cpp`: batch append, durability mode.
- `include/mosaic/match/matcher.h`: benchmark harness only (no behaviour change).
- Tests: benchmarks with asserted regression bounds (generous, to avoid flakiness), plus correctness tests for the two fixes.
- `docs/design/03-matching-and-channels.md`, `include/mosaic/store/README.md`: numbers replace claims.
- `docs/design/TODO.md`: MATCH-1, LOG-2 close; STORE-2/STORE-3 remain open and are referenced.
