# Proposal

## Why

Two hot paths were measured in `measure-and-optimize` and both were left with a known, specific inefficiency rather than a
vague "could be faster".

- `Matcher::matches` copies the `open` bitset at every pattern position to decide whether a channel segment may be skipped
  (MATCH-4). At 100k subscriptions that copy dominates: 6.7 / 22.7 / 191.7 µs at 1k / 10k / 100k subscriptions, growing worse
  than linearly for a lookup that should be linear.
- `ReplicaLog::ingest` processes one record at a time, so a repair reply of 256 records makes 256 store calls even though
  `LogStore::append_batch` exists and costs 1.9-5.0 µs per record against 648 µs per record for individual durable appends
  (LOG-2).

Neither is a correctness problem and neither needs new behaviour: this change is about making the numbers honest.

## What Changes

- **Matcher**: replace the per-position `open` copy with a reusable scratch bitset, and exit a position word-wise instead of
  segment by segment. Matching results MUST be bit-identical to today, which a property test over generated patterns asserts
  exhaustively.
- **Log**: add `ReplicaLog::ingest(span<const Record>, now_ms)` that validates a run of records and appends them with one
  `append_batch` call, stopping at the first record that is not next and reporting the outcomes for all of them.
- **Numbers**: the benchmark records before/after at 1k / 10k / 100k subscriptions and for repair-sized batches, and
  `performance/measured-cost` carries the new figures plus the machine and compiler they came from.

## Capabilities

### New Capabilities

- None.

### Modified Capabilities

- `match/channel-patterns`: matching must not allocate per lookup, and must produce exactly the same results.
- `log/replicated-log`: ingest accepts a batch with per-record outcomes.
- `performance/measured-cost`: the published matcher and ingest costs are the post-change ones, measured the same way.
