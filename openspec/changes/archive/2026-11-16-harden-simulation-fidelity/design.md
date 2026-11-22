# Design

## Context

See proposal.md - Why. Relevant constraints:

- `SimNetwork` today is fully connected, seeded, and delivers **immediately**: `deliver()` pushes into the target's inbox
  during `publish()`. Time is owned by the test (`pump(now)` / `tick(now)`), and the node never reads a clock.
- `Node` keeps `peer_credits_` (a `CreditWindow` per peer) and `fetches_` (outstanding requests), and on timeout calls
  `credits_for(peer).reset()` (`source/node/node.cpp:73`, `TODO(FLOW-2)`).
- Everything is single-threaded and standard-library only; `core`..`node` may not take dependencies.
- The convergence test is one `SimNetwork net(7)` run.

## Goals / Non-Goals

**Goals:**

- Make the harness capable of producing the failures a real network produces, and make those failures reproducible.
- Turn "the follower converged" into an assertion with counters behind it rather than an inference from log contents.
- Close the credit over-granting defect that a delayed-response scenario exposes.

**Non-Goals:**

- No attempt to model TCP, bandwidth, socket buffers or real ZeroMQ HWM behaviour. The harness models the *contract*
  (loss, duplication, delay, reorder, partition), not any implementation of it.
- No performance benchmarking of the node. MATCH-1 (matcher throughput) stays on the TODO board.
- No new protocol messages, no wire encoding, no transport implementation. This change makes the layer below trustworthy
  before the layer above is written.
- No threads. A deterministic simulation is single-threaded by definition; TSan work belongs with a threaded transport.

## Decisions

**D1 - Delivery is scheduled, not immediate.**
`SimNetwork::deliver` becomes `schedule(delivery_time, ...)` into a time-ordered queue keyed by `(time, sequence)`. A
`pump(now)` releases everything due. This is what makes latency, reordering and timeouts testable at all.
*Alternative:* keep immediate delivery and inject delay by counting pump iterations - rejected: it makes "delay" mean
"number of loop turns", which is not a property anyone can reason about.

**D2 - Each link-direction is its own random stream.**
Loss, duplication, delay and reordering draws come from a per-`(from,to)` stream derived from the run seed rather than from
one shared generator. *Why:* adding a link must not perturb the draws of unrelated links, otherwise every scenario change
reseeds the whole world and seed numbers become meaningless across edits. *Alternative:* one shared generator - simplest,
but makes seeds fragile and defeats "a reported seed reproduces the failure".

**D3 - Reordering is produced by jittering delay, not by shuffling queues.**
Each message gets a delay drawn from a configurable range; ties are broken by send sequence. Reordering therefore emerges
from the same mechanism as latency, which keeps one code path and one fault knob. A scenario that wants deterministic
inversion configures a range wide enough to guarantee it, and the requirement "at least one pair is inverted" is checked by
the suite itself.

**D4 - Partitions are an adjacency matrix, not a special message type.**
Dropping is decided at schedule time against a reachability matrix that a scenario can mutate mid-run. Sending into a
partition is a silent drop, exactly like loss, so no new node code path is needed and the node cannot "know" it is
partitioned - which is the honest model.

**D5 - Counters are a plain value struct on the node, reset-free.**
A `NodeCounters` aggregate of `std::uint64_t` members, exposed by const reference. No logging, no callbacks, no string
formatting in the hot path. *Why:* tests assert; humans read the struct in a debugger. *Alternative:* callback per event -
rejected as it adds an interface every module would have to thread through for a test-only need.

**D6 - Credit is owed per outstanding request.**
`fetches_` entries already carry `{sent_ms, end}`. Add the credit owed (the requested count) to the entry; on completion
return exactly that amount; on timeout return it too, and mark the entry abandoned so a late response is matched and
discarded rather than re-crediting. The window becomes a *derived* value: capacity minus the sum of outstanding debts.
*Alternative:* a request id in the protocol - rejected here because the repair path's `(from,count)` is unique per peer and
adding a field to the message set belongs to ENC-1, not to a flow fix.

**D7 - The conformance suite is a function over a factory, not a test macro.**
`run_transport_conformance(factory)` as a normal function called from one `TEST_CASE` per implementation. *Why:* a macro
hides which assertions failed; a function gives subcases and readable failures. This is the pattern STORE-0 adopts too.

**D8 - The scenario runner is a test-side component, exposed to the CLI only as an optional target.**
`Mosaic sim` is a convenience, not a product surface; it links the test-side runner and is excluded from the library target
so no scenario code can creep into `Mosaic`/`MosaicZmq`.

## Risks / Trade-offs

- **Scheduled delivery changes every existing scenario's timing** → The three existing node scenarios are rewritten first, in
  the same commit as the network change, so a semantic regression cannot hide behind a mechanical rewrite.
- **Seed stability across code edits** (a new draw shifts everything) → Per-link streams (D2) contain the damage to the
  edited link. Sweeps assert a *count* of passing seeds, not specific seeds, so the suite does not pin numbers it can't keep.
- **Sweeps slow the suite** → Default sweep size stays small (tens of seeds) in `MosaicTests`; the large sweep is an opt-in
  flag and a CI job, so the fast loop stays under a few seconds.
- **Counters creep into product logic** → Counters are written at the points where a decision has already been made; review
  rejects any counter that changes control flow.
- **Per-request credit bookkeeping gets subtly wrong** → The invariant "available never exceeds capacity" is asserted after
  every step in the flow property test, plus the delayed-response scenario; both fail loudly on an off-by-one.
- **Simulation and reality diverge** → Named explicitly: the harness proves the protocol tolerates faults, not that ZeroMQ
  tolerates them. NET-0 plus the ZeroMQ conformance run is the bridge, and it is a later change.

## Migration Plan

1. Land the network change and rewrite existing scenarios (fast loop green).
2. Add counters with no behaviour change; assert them in existing scenarios.
3. Add the delayed-response scenario; it is expected to **fail** and expose the over-granting.
4. Fix credit accounting; the scenario goes green; add the flow property test.
5. Add the seed sweep and the conformance suite refactor.

Rollback is per-step: steps 1-2 are additive, and step 4 is isolated to `fetches_`/`peer_credits_`.

## Open Questions

- ~~Whether `Mosaic sim` should exist at all at this stage~~ **Resolved: not built.** The scenario runner lives in the test
  sources, so a `Mosaic sim` subcommand would have to link test code into the product or duplicate the scenarios. The sweep is
  `MOSAIC_SEEDS=<n> ./build/test/MosaicTests -tc="*seed sweep*"`, which is one documented command; the CLI half of SIM-2 stays
  open on the board and should move to the CLI change, where a real command surface exists.
- Whether per-link fault configuration needs a declarative file format for long scenarios, or whether in-code scenario
  functions remain enough. Deferrable until a scenario needs more than a page of configuration.
