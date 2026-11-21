# Design

## Context

`Matcher` stores patterns in a dotted trie: per node a `Bits` of subscription ids and an `open` bitset marking "a pattern ends
here". `matches(channel)` walks segments; at each position it currently copies `open` to compute which subscriptions can still
match, because the walk mutates it as it descends.

`ReplicaLog::ingest(Record)` verifies, appends one record, and reports `IngestResult`. `LogStore::append_batch` exists (added in
`measure-and-optimize`) with 1.9-5.0 µs per record against 648 µs per durable single append; the log layer does not use it.

Measured today: 6.7 / 22.7 / 191.7 µs at 1k / 10k / 100k subscriptions.

## Decisions

### D1 - One scratch bitset per matcher, not per call

The copy exists because `matches` needs a working set per position that starts as `open`. Instead of copying, keep two member
bitsets (`scratch_`, `scratch_next_`) that are `resize`d to fit and then reused: `clear()` is a word loop, and the result is the
same set. Determinism is preserved because every word is written before it is read (`make_words` below sets or clears each word
explicitly).

The matcher is single-threaded by contract (`conventions.md`), so a mutable scratch buffer does not need synchronisation; the
class documents that a `Matcher` is not shareable across threads, which was already true of `add`/`remove`.

### D2 - Exit a position word-wise

The skip test "is there any subscription still open past this segment" is a bitwise AND of the current set against the node's
`open` mask followed by a search for any set bit. That is one AND plus one compare per 64 subscriptions; doing it once per 64
subscriptions instead of once per subscription is what removes the worse-than-linear growth. `countr_zero`/`find_first_set`
then find the exact index only when the result is needed.

### D3 - Equivalence is proven by a reference implementation, not by inspection

`matches` gets a test-only reference path (straight interpretation of each pattern against the channel, no trie) and a property
test over generated patterns: segments, `*`, `>`, nested, empty, near-miss. The optimised path must agree with the reference on
every pair. Without this, "same results" is an assertion; with it, it is a test.

### D4 - Batch ingest stops at the first gap and says so

`ingest(span, now)` verifies and collects records into a run, calling `append_batch` per log once the run is closed. A gap or a
refused record closes the run; the remaining records get an explicit `NotAttempted` outcome rather than being silently skipped,
because the caller (repair) uses the outcomes to decide what to ask for next. Ambiguous outcomes would turn a missing record into
an invisible one.

Records for different logs inside one batch (repair can carry several logs) are grouped by `LogKey` and batched per log, in arrival
order per log - which is what makes "equivalent to one-at-a-time" true rather than approximately true.

### D5 - The numbers are re-measured in the same harness

The benchmark keeps its command and its shape; only the figures change. Both before and after values go in the change's
description of `performance/measured-cost`, with the machine, compiler and flags, because a number without those is not a
measurement.

## Risks

- **A shared scratch bug would be data-dependent**: an empty match on some pattern set. Contained by the reference-implementation
  property test, which is exhaustive over the generated corpus.
- **Batch ingest could reorder across logs.** Grouping per log preserves per-log order; across logs there is no order to preserve
  (`log/replicated-log`: ordering exists only inside a log).
- **A bigger batch means a longer window of uncommitted work** under `Durability::Batched`. The loss window is documented and
  unchanged: it is the flush interval, not the batch size.
