# Tasks

## 1. Size and pattern bounds

- [ ] 1.1 Enforce a record size limit in ingest and publish, reporting the refusal. Verified by: an oversized record is not
      stored, and a local publish of one returns a failure rather than writing it.
- [ ] 1.2 Cap the patterns a single head request may contain. Verified by: a request above the cap is refused whole, with no
      matcher constructed.

## 2. Per-peer budgets

- [ ] 2.1 Rate-limit head and repair requests per peer, counting the surplus. Verified by: a scenario where a peer asks far more
      often than the interval keeps queue depth bounded.
- [ ] 2.2 Bound pending jobs in the worker and count the overflow. Verified by: a burst of requests leaves the pending set at its
      cap.
- [ ] 2.3 Prove throttling is not a partition: a throttled legitimate peer still converges. Verified by: a scenario asserting
      convergence after a burst.

## 3. Close out

- [ ] 3.1 Close SEC-2's limits half on the board and record that scoring remains open. Verified by: the board and
      `./scripts/check_todo_ids.sh`.
- [ ] 3.2 Definition of done: four build combinations, clang-format clean, spec row updated. Verified by: the matrix and format
      check.
