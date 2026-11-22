# Tasks

## 1. The segment backend

- [x] 1.1 Format header (`HEAD`), segment header, record frame with CRC, and the index layout, documented in
      `include/mosaic/store/README.md` and in the site's store page. Verified by: a reader written against the documentation alone
      recovers records from a store the tests produced.
- [x] 1.2 `SegmentLogStore` implementing `LogStore`: append, append_batch, head, get, read, keys, with segment rollover.
      Verified by: the shared conformance suite passes unchanged.
- [x] 1.3 Recovery on open: truncate a torn frame, rebuild a disagreeing index from the segment, report the recovered head, and
      refuse an unknown format version. Verified by: crash-simulation tests that write a partial frame and reopen.
- [x] 1.4 `Durability::PerRecord` fsyncs before returning; `Batched` states its window and is measured. Verified by: the
      durability tests parameterised over both modes, and a benchmark line in `docs/design/08-testing-strategy.md`.

## 2. Checkpoints and truncation

- [x] 2.1 Checkpoint codec: signed `(seq, hash)` with format and algorithm bytes, round-trip and hostile-input tests.
      Verified by: a truncated payload is refused, not guessed.
- [x] 2.2 `LogStore::truncate_before(seq, checkpoint)` dropping whole segments and advancing the store's origin; the checkpoint
      survives reopen. Verified by: reads start at the checkpoint's successor and the checkpoint is still retrievable.
- [x] 2.3 `ReplicaLog` verifies a record against the store origin when the predecessor is gone. Verified by: a replica truncated
      at 99 verifies 100 when `prev` matches the checkpoint and refuses it when it does not.
- [x] 2.4 `Mosaic checkpoint <channel>` writes and publishes a checkpoint; `Mosaic verify` accepts a truncated store and reports
      its origin. Verified by: CLI test over a truncated store.

## 3. Retention policy

- [x] 3.1 Retention evaluation: age, size, complete segments, and `min_replicas` when set, refusing when anything is unknown.
      Verified by: no drop without acknowledgements; drop when all conditions hold; the count of dropped records is reported.
- [x] 3.2 Daemon configuration for per-log retention (`retention.max_age`, `retention.max_bytes`, `retention.min_keep`) with
      validation and a defaults printer entry. Verified by: config validation test and a daemon run that truncates on tick.
- [x] 3.3 Retention never breaks fork detection or verification. Verified by: a fork in a truncated log is still reported.

## 4. Close out

- [x] 4.1 `docs/design/01-data-model.md` documents the segment format and the origin rule; `store/README.md` loses the STORE-2/3
      open-work note. Verified by: the docs name the format byte and the checkpoint payload.
- [x] 4.2 Close STORE-2 and STORE-3 on the board; `./scripts/check_todo_ids.sh`; `./scripts/ci.sh` green (the segment backend
      builds in the dependency-free configuration too), site rebuilt.
