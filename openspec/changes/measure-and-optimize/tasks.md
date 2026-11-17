# Tasks

## 1. Measure

- [ ] 1.1 Add the matcher benchmark (1k / 10k / 100k subscriptions) with printed numbers and a generous asserted bound. Verified
      by: the command in the docs reproduces the numbers, and MATCH-1 closes with them recorded in 03-matching.
- [ ] 1.2 Add a per-tick cost benchmark that grows the store and asserts the query count per tick does not grow. Verified by: the
      test failing against today's implementation before the fix.

## 2. Remove work that scales with volume

- [ ] 2.1 Drive acknowledgements from recently ingested logs instead of a store inventory scan. Verified by: a test asserting no
      store query per tick for a store with many logs but no new data.
- [ ] 2.2 Compute the retention floor as the k-th largest acknowledgement. Verified by: existing retention and shortfall scenarios
      passing unchanged, plus a unit test over acks with a large sequence range.

## 3. Durability and batching

- [ ] 3.1 Add named durability modes with the crash-loss guarantee documented; default unchanged. Verified by: the conformance
      suite passing in both modes and the default surviving an immediate reopen.
- [ ] 3.2 Batch append in `LogStore` and the SQLite backend, with the conformance suite extended to require batch == N singles.
      Verified by: a test appending a span and reading it back identically.
- [ ] 3.3 Re-measure append throughput per mode and update the store README numbers. Verified by: the command and the numbers
      matching a fresh run.

## 4. Close out

- [ ] 4.1 Close MATCH-1 and LOG-2 on the board; note STORE-2/STORE-3 as still open. Verified by: board and
      `./scripts/check_todo_ids.sh`.
- [ ] 4.2 Definition of done: four build combinations, clang-format clean, measurement commands documented. Verified by: the
      matrix and the format check.
