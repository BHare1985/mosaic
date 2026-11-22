# Proposal

## Why

Mosaic replicates well while the writer is running and badly the moment it is not. Three gaps, all of which show up in the
project's own use cases (edge sites, local-first sync, audit logs over unreliable links):

1. **Nothing confirms that a record survived.** A writer publishes and moves on; if every follower was unreachable, the record
   exists in exactly one place and the writer has no way to know. Dafka had the same hole - it shipped no `min_replicas`.
2. **A follower that joins while the writer is away cannot find out what it is missing.** Heads arrive only from the writer's
   heartbeat, so a replica that already holds the log can answer repair requests but nobody knows to ask it.
3. **The live path has no back-pressure vocabulary.** A follower that falls behind accumulates whatever the socket queue will
   hold, then loses records silently and repairs later, which is work it did not have to do.

This change makes the log survive its writer: replicas acknowledge, the writer keeps records until enough replicas have them,
newcomers discover heads from peers, and slow followers are told to catch up instead of being fed.

## What Changes

- Replicas acknowledge the sequence they hold; a writer tracks the highest acknowledged sequence per log.
- The writer retains records until `min_replicas` distinct replicas have acknowledged them, and reports what it is waiting on.
- A newcomer can ask peers for their heads, so a log can be found and repaired with the writer offline.
- Live delivery gains a credit path and a slow-follower rule: a follower too far behind stops receiving live records and
  catches up by fetch, keeping memory bounded.
- **BREAKING** for the control plane only: `AckMsg`, `GetHeadsMsg`, `DirectHeadMsg` and `CreditMsg` stop being reserved
  placeholders and gain behaviour (their ids were allocated in the wire format precisely for this).
- **Not** in scope: consensus, leader election, multi-writer logs, exactly-once. This is retention and discovery around a
  single-writer log, nothing more.

## Capabilities

### New Capabilities

- `log/retained-until-replicated`: the acknowledgement protocol and the writer's retention rule, including what happens when
  `min_replicas` is never reached.

### Modified Capabilities

- `node/node-runtime`: head exchange with peers (newcomers learn heads without the writer), and observable retention state.
- `flow/credit-control`: credit on the live path, and the slow-follower rule that moves a lagging follower to repair-only.

## Impact

- `include/mosaic/node/node.h`, `source/node/node.cpp`: ack tracking, retention, head exchange, slow-follower policy.
- `include/mosaic/proto/messages.h` (values already exist), codec round-trips for the four messages.
- `include/mosaic/store/log_store.h` possibly gains a `truncate_below` for retention (design decision).
- Tests: sim scenarios for writer-offline joins, ack-driven retention, and a follower slower than the writer.
- `docs/design/TODO.md`: ACK-1, HEADS-1, FLOW-3, FLOW-4 close; `min_replicas` becomes a real configuration knob.
