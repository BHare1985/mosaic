# Tasks

## 1. Acknowledgements and retention

- [x] 1.1 Handle `AckMsg` in the node: track the highest acknowledged sequence per (log, peer), idempotently. Verified by: a sim
      scenario where repeated and out-of-order acks leave the writer's view at the maximum.
- [x] 1.2 Send `AckMsg` from a replica after storing records, batched to at most one per interval per log. Verified by: a
      follower's acks reach the writer and converge to its head.
- [x] 1.3 Add per-log `min_replicas` and the retention decision, with a bound and a shortfall counter. Verified by: records are
      retained until two replicas ack, then released; with no replica reachable the bound applies and the counter rises.
- [x] 1.4 Expose retention state: records held for replication and logs below their floor. Verified by: a test reads it while
      replication is unmet.

## 2. Head exchange

- [x] 2.1 Answer `GetHeadsMsg` with one `DirectHeadMsg` per matching log held, capped at `max_serve`. Verified by: a peer asks
      with a pattern and receives exactly the logs it holds.
- [x] 2.2 Ask peers for heads when a node follows a new pattern, and treat answers as repair hints. Verified by: a newcomer
      converges from a replica while the writer is unreachable.
- [x] 2.3 Rate-limit head requests per peer. Verified by: a scenario asserting one request per interval per peer.

## 3. Live-path flow control

- [x] 3.1 Handle `CreditMsg` and apply granted credit to live delivery on that link. Verified by: ten granted credits admit ten
      records and no more until more arrive.
- [x] 3.2 Keep best effort when no credit has been granted. Verified by: the existing loss scenarios still pass unchanged.
- [x] 3.3 Slow-follower rule: when `known_head - next_seq` exceeds the threshold, stop accepting live records for that log and
      repair by fetch; resume when caught up. Verified by: a follower slower than the writer converges with a bounded backlog.

## 4. Prove it and close the board

- [x] 4.1 Sim scenario: writer goes offline after publishing, a late joiner converges from a replica alone. Verified by: the new
      scenario plus the existing 8-seed sweep staying green.
- [x] 4.2 Sim scenario: one replica dies mid-run; retention with `min_replicas = 2` reports the shortfall and the log survives on
      the other. Verified by: the scenario asserts both the counter and the surviving log.
- [x] 4.3 Close ACK-1, HEADS-1, FLOW-3, FLOW-4 on the board with the M4 row updated. Verified by: `./scripts/check_todo_ids.sh`
      exits 0 and no `TODO(ACK-1|HEADS-1|FLOW-3|FLOW-4)` remains in code.
- [x] 4.4 Definition of done: four build combinations green, clang-format clean, spec rows updated. Verified by: the matrix and
      the format check.
