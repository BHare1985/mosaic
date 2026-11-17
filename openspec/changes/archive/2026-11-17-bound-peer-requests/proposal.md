# Proposal

## Why

Links are now encrypted and authenticated (CURVE + ZAP pinning), records are verified, and replication converges. What is missing
is any limit on what an *authenticated* peer can make a node do. Three holes, all found by reading the code:

1. **`kMaxRecordBytes` is declared and never enforced.** Frame size is bounded at 1 MiB, record size is not - neither at ingest
   nor on publish. A peer can make a replica store a megabyte per record; a local writer can publish more.
2. **`GetHeadsMsg` builds a `Matcher` from untrusted patterns, per request.** Bounded only by the frame limit (~250k patterns),
   with no cap and no rate limit, so one peer can repeatedly force an expensive allocation and parse.
3. **Job requests are effectively unbounded.** A peer can publish requests faster than workers retire them, and the pending set
   grows without limit - the same failure FLOW-4 fixed on the live path, still present one layer up.

None of this needs authorization to fix. It is bounds and quotas, and it should land before anyone points this at a network they
do not control.

## What Changes

- A record size limit enforced at ingest *and* publish, with an explicit error rather than a silent drop.
- A cap on patterns per head request, and a per-peer rate limit for repair and head traffic (SEC-2's scoring/limits, minus the
  scoring heuristics).
- Bounded pending jobs per worker, with an overflow counter.
- A peer that exceeds its budget is throttled and counted, not disconnected - one noisy peer must not be able to look like a
  network partition.
- **Not** in scope: bad-peer scoring with time-based bans (SEC-2's heuristics), authorization (separate change), and CURVE-level
  admission control (already done in SEC-1b).

## Capabilities

### New Capabilities

- `security/abuse-limits`: the bounds an authenticated peer operates under, and what a node does when one is exceeded.

## Impact

- `include/mosaic/node/node.h`, `source/node/node.cpp`: ingest/publish size checks, pattern cap, per-peer budgets, pending job cap.
- `include/mosaic/log/log.h`: the record limit is a property of the log layer, applied before any store write.
- `include/mosaic/jobs/runtime.h`: bounded pending set.
- Tests: sim scenarios for an oversized record, a hostile head request, and a peer flooding requests.
- `docs/design/TODO.md`: SEC-2 closes (its limits half; scoring stays open).
