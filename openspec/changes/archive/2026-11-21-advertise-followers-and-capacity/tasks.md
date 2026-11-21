# Tasks

## 1. Advertisement

- [x] 1.1 Extend `HelloMsg` with spare capacity (one byte) and bump `kProtocolVersion` to 2, with golden vectors updated for the
      new field. Verified by: codec round-trip and a golden vector that fails when the field is dropped.
- [x] 1.2 Send the subscription patterns and the capacity figure in `greet_peers`/`tick`, bounded by `max_head_patterns`, and
      recomputed only on the hello interval. Verified by: a sim test asserting a peer's hello carries its patterns.
- [x] 1.3 Record per-peer patterns and capacity in `peer_links_` in `handle_hello`, replacing the previous set, with the
      truncation counted. Verified by: a later hello replaces an earlier set (no accumulation).
- [x] 1.4 Treat an advertisement as stale after N hello intervals and expire it. Verified by: a peer that stops sending hellos
      loses its advertised patterns.

## 2. Accessors and counting

- [x] 2.1 Expose `Node::peer_patterns(peer)` and `Node::peers_following(pattern)` over the same table. Verified by: unit tests
      for both, including an unknown peer.
- [x] 2.2 Add counters: `patterns_truncated`, `capacity_unknown`, `hello_version_mismatch`. Verified by: asserted in the sim
      tests that produce each condition.

## 3. Jobs use the advertisement

- [x] 3.1 Derive `JobWorker`'s worker set from `peers_following(service_channel)` plus the node itself when it follows the
      service; keep the explicit-list constructor as the override. Verified by: three workers on a sim network, no explicit
      list, exactly one runs the job.
- [x] 3.2 Prefer workers advertising spare capacity by moving saturated workers behind unsaturated ones, keeping a total order.
      Verified by: a saturated rank 0 is not asked first; all-saturated still sends.
- [x] 3.3 Verify capacity cannot cause duplicate execution: at most one run per attempt when capacity changes mid-flight.
      Verified by: a sim scenario that flips capacity between ticks.

## 4. Close out

- [x] 4.1 Update `docs/design/02-protocol.md` (hello contents) and `docs/design/06-jobs.md` (derived worker set), plus the jobs
      and node READMEs. Verified by: the docs name the version bump and the hint rule.
- [x] 4.2 Add an `openspec/specs` delta check: the jobs spec and the new advertisement spec both validate.
- [x] 4.3 Close HELLO-1 and JOBS-4 on the board; `./scripts/check_todo_ids.sh`; `./scripts/ci.sh` green in all four
      configurations, with the site rebuilt.
