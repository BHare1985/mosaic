# Proposal

## Why

The system's headline promise - "whatever the network does, followers converge" - is currently witnessed by exactly one
test under one fault model: `converges under 30% loss and 10% duplication` uses seed 7, and the simulated network has no
latency, no reordering and no partitions. A green suite therefore proves very little about the repair state machine, and
nobody can tell a robust design from a lucky one. Two behaviours that the design claims (late responses must not inflate
credit; the credit window is bounded per peer) are not assertable at all today, because the node exposes no counters.

This change makes the harness able to *falsify* before more is built on top of it, and fixes the one flow-control defect the
better harness will expose.

## What Changes

- The simulated network gains latency, reordering, per-link bidirectional loss and partitions with healing, all seed-driven.
- A scenario runner sweeps seeds, reports the first failing seed, and reproduces it exactly from that seed alone.
- The node exposes counters (ingest outcomes, fetches sent, repair retries, records dropped) so flow-control and repair
  behaviour can be asserted instead of eyeballed.
- Per-request credit accounting replaces the blanket window reset on timeout, so a merely late response cannot over-credit a
  peer.
- A transport conformance suite is extracted so every transport, including the future ZeroMQ one, is held to the same rules.
- **BREAKING** for tests only: existing node scenarios are re-expressed through the scenario runner; no product behaviour is
  removed.

## Capabilities

### New Capabilities

- `harness/simulation`: a deterministic, fault-injecting, seed-reproducible network and scenario runner that is the only
  accepted evidence for system-level requirements.

### Modified Capabilities

- `node/node-runtime`: adds an observable-counters requirement, and strengthens the repair requirement so that outstanding
  credit is accounted per request rather than per peer. Depends on the baseline change `establish-capability-baseline`
  (which creates the capability); this change adds requirements to it, so archive baseline first.
- `net/transport`: adds the requirement that the conformance suite is a reusable artefact that runs any transport, rather
  than a scenario that happens to exercise the simulated one.

## Impact

- `include/mosaic/net/sim_network.h`, `source/net/sim_network.cpp`: latency, reorder, per-link loss, partitions, scheduled
  delivery.
- `include/mosaic/node/node.h`, `source/node/node.cpp`: counters accessor, per-request credit accounting.
- `test/source/test_node_sim.cpp`: scenarios move to the runner and gain seed sweeps.
- New test-side scenario runner; the dev command `Mosaic sim` is optional and stays out of the product library.
- `docs/design/TODO.md`: SIM-1, SIM-2, NET-0, NODE-1 and FLOW-2 close out of the board.
- No change to `core`, `store`, `log`, `match` or the message set. No new dependency.
