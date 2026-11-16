# Tasks

## 1. Scheduled delivery and per-link faults (SIM-1)

- [x] 1.1 Replace immediate delivery with a time-ordered delivery queue keyed by (due time, send sequence); release due
      messages in the pump step. Verified by: all three existing node scenarios still pass in the fast loop.
- [x] 1.2 Give each link direction its own generator stream derived from the run seed. Verified by: adding a peer to a
      scenario does not change the drop pattern of an existing link (assert with two runs, one with an extra isolated peer).
- [x] 1.3 Add delay with configurable jitter so reordering emerges; add per-link, per-direction loss and duplication.
      Verified by: a test where at least one message pair arrives inverted, and a test where a delayed message is not
      visible to the receiver before its due time.
- [x] 1.4 Add a reachability matrix for partitions that a scenario can mutate mid-run. Verified by: a partition-then-heal
      scenario where nothing crosses during the partition and both sides converge after healing.
- [x] 1.5 Keep the harness inside the standard library and out of the product library target. Verified by: building with
      the simulation target excluded still builds and passes unit tests; `rg -n "zmq" include/mosaic/net/sim_network.h`
      returns nothing.

## 2. Counters (NODE-1)

- [x] 2.1 Add a `NodeCounters` value struct (accepted, duplicate, gap, fork, bad signature/bad chain, fetch requests sent,
      retries after timeout, drops for exhausted credit) exposed by const reference. Verified by: no control flow reads a
      counter; the struct is returned to tests by accessor.
- [x] 2.2 Increment counters at the existing decision points in ingest and repair. Verified by: a scenario with two valid,
      one duplicate and one forged record reports exactly those counts, and the app callback count matches.
- [x] 2.3 Assert counters in every existing node scenario. Verified by: the scenarios fail if a counter is removed from the
      increment path (spot-check by comment-out locally, do not commit).

## 3. Per-request credit accounting (FLOW-2)

- [x] 3.1 Write the failing scenario first: a link that delays every response past the request timeout while losing nothing.
      Verified by: the test fails on the current blanket reset, with available credit exceeding capacity at some step.
- [x] 3.2 Track credit owed on each outstanding request and return exactly that amount on completion or abandonment; treat a
      response for an abandoned request as consumed and discard it. Verified by: the delayed-response scenario passes and
      never observes available credit above capacity.
- [x] 3.3 Add a property test over randomized take/grant/timeout sequences asserting the invariant available <= capacity and
      available >= 0 after every step. Verified by: the property test runs at least a thousand randomized steps.

## 4. Seed sweep and scenario runner (SIM-2)

- [x] 4.1 Extract scenarios into named functions the runner can invoke with a seed, with no sleeping and simulated time only.
      Verified by: a scenario that simulates an hour of partition completes without wall-clock delay.
- [x] 4.2 Add a sweep helper that runs a scenario over N seeds, reports the first failing seed plus the failing assertion, and
      reports the count on success. Verified by: an intentionally broken scenario fails with its seed, and re-running that seed
      reproduces it; an all-passing sweep reports its seed count.
- [x] 4.3 Run the convergence scenario across a default sweep in the fast loop and a larger sweep behind a flag.
      Verified by: `./build/test/MosaicTests` stays quick with the default 8-seed sweep; the larger sweep is
      `MOSAIC_SEEDS=1000 ./build/test/MosaicTests -tc="*seed sweep*"`. The `Mosaic sim` CLI named in SIM-2 is **not** built
      here (design D8 and its open question): the sweep runs from the test binary, and the board row is marked partial.

## 5. Transport conformance suite (NET-0)

- [x] 5.1 Express the transport contract as `run_transport_conformance(factory)` covering best-effort delivery, absence of
      self-delivery, publication filtering, silent unicast drop, stable identity and non-blocking poll. Verified by: the
      simulated transport runs it as one test case, with no transport-specific branches in the suite.
- [x] 5.2 Note in `docs/design/05-node-and-transport.md` that the suite is the acceptance gate for any new transport.
      Verified by: the sentence exists and names the function.

## 6. Close the board entries and prove the gate

- [x] 6.1 Mark SIM-1, NET-0, NODE-1 and FLOW-2 done in `docs/design/TODO.md` with the milestone row updated; SIM-2 is
      marked partial because its `Mosaic sim` CLI half is deferred.
      Verified by: `./scripts/check_todo_ids.sh` exits 0 and no `TODO(SIM-*|NET-0|NODE-1|FLOW-2)` remains in code.
- [x] 6.2 Prove the gate is real: temporarily reintroduce the blanket credit reset and confirm the suite catches it.
      Verified by: `credit is accounted per request` fails with `credit_available("writer") == 2` while `outstanding == 0`;
      the probe was reverted. The first version of this test did *not* catch it (it derived outstanding from the live-request
      table, which the buggy path empties), so it now measures cumulative `credit_spent`/`credit_returned` instead.
- [x] 6.3 Run the full definition of done for this change: fast loop plus ZeroMQ-stack build, clang-format clean, no new
      dependency, every new header comment states its contract. Verified by: both builds green (31 cases, 6008 assertions)
      and `clang-format --dry-run --Werror` clean on every file this change touched.
      Note: the gate could not pass at all before, because `.clang-format` said `UseTab: Always` while `conventions.md` and
      all code use 4 spaces. The config now says `UseTab: Never`; 7 untouched files still carry drift and are tracked as
      CI-4 rather than reformatted inside this change.

## Workflow follow-up

- `verifiable-records-and-wire-format` depends on this change's counter names only for test assertions; re-check the struct
  before adding fields for encoding errors (a decode-failure counter belongs to that change, not this one).
