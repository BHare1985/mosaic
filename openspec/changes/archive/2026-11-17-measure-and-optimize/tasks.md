# Tasks

## 1. Measure

- [x] 1.1 Add the matcher benchmark (1k / 10k / 100k subscriptions) with printed numbers and a generous asserted bound. Verified
      by: `./build/test/MosaicTests -tc="*matcher cost*"` reproduces the numbers, recorded in 03-matching. The measurement
      **disproves the inherited claim**: 6.7 / 22.7 / 191.7 us rather than the "~2 us" the design set carried, growing linearly
      in subscriptions - which is what O(segments x subscribers/64) predicts. `matches_any` no longer materialises the id list
      (worth 6-29%); the remaining cost is per-position bitset work, filed as MATCH-4.
- [x] 1.2 Add a per-tick cost benchmark that grows the store and asserts the query count per tick does not grow. Verified by: the
      test failing against today's implementation before the fix.

## 2. Remove work that scales with volume

- [x] 2.1 Drive acknowledgements from recently ingested logs instead of a store inventory scan. Verified by: a test asserting no
      store query per tick for a store with many logs but no new data.
- [x] 2.2 Compute the retention floor as the k-th largest acknowledgement. Verified by: existing retention and shortfall scenarios
      passing unchanged, plus a unit test over acks with a large sequence range.

## 3. Durability and batching

- [x] 3.1 Add named durability modes with the crash-loss guarantee documented; default unchanged. Verified by: the conformance
      suite passing in both modes and the default surviving an immediate reopen.
- [x] 3.2 Batch append in `LogStore` and the SQLite backend, with the conformance suite extended to require batch == N singles.
      Verified by: a test appending a span and reading it back identically.
- [x] 3.3 Re-measure append throughput per mode and update the store README numbers. Verified by: the command and the numbers
      matching a fresh run.

## 4. Close out

- [x] 4.1 Close MATCH-1 on the board; note STORE-2/STORE-3 as still open. Verified by: board and
      `./scripts/check_todo_ids.sh`. **LOG-2 is narrowed, not closed:** this change added `LogStore::append_batch` - the
      primitive that makes one commit cover a repair range - but `ReplicaLog::ingest` still takes one record at a time, so the
      log layer has no batching to use it. Recorded on the board rather than claimed.
- [x] 4.2 Definition of done: four build combinations, clang-format clean, measurement commands documented. Verified by: the
      matrix and the format check.
