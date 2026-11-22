# Tasks

## 1. Matcher

- [x] 1.1 Add the reusable scratch bitsets and keep `matches` allocation-free in steady state. Verified by: an allocation
      counter (or a `new`-intercepting test hook) asserts zero allocations after a warm-up match.
- [x] 1.2 Replace the per-position copy with the word-wise skip test and early exit. Verified by: the reference-implementation
      property test agrees on every generated pattern/channel pair.
- [x] 1.3 Measure: 1k / 10k / 100k subscriptions before and after, on the same machine, recorded in the design's numbers table.
      Verified by: the benchmark's output.

## 2. Batch ingest

- [x] 2.1 `ReplicaLog::ingest(std::span<const Record>, now_ms)` returning one outcome per record, grouping by log and stopping at
      the first gap. Verified by: contiguous run, gap in the middle, refused record in the middle, several logs in one batch.
- [x] 2.2 Equivalence test: the same records ingested individually and in batch store the same history, deliver the same records
      in the same order, and move the same counters. Verified by: a property test over generated runs.
- [x] 2.3 Use the batch path on the repair reply path so a fetch reply of N records costs one store call per log. Verified by: a
      scenario with a bounded store that counts `append` calls.
- [x] 2.4 Measure batched ingest cost per record for repair-sized batches. Verified by: benchmark output added to the published
      figures.

## 3. Close out

- [x] 3.1 Update the published figures and the machine/compiler note in `docs/design/08-testing-strategy.md` and the site's
      performance page. Verified by: the numbers match the benchmark.
- [x] 3.2 Close MATCH-4 and LOG-2 on the board; `./scripts/check_todo_ids.sh`; `./scripts/ci.sh` green in all four
      configurations.
