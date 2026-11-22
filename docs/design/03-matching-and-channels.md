# 03 — Matching & channels (✅ implemented, including the transport mapping)

_Describes 0.7.0._

## Channels
A channel is a dotted name: `storage.eu.shard7`. Segments are non-empty; `*` and `>` are reserved for patterns.
Convention for naming: `<domain>.<scope>.<name>`; jobs use `svc.<service>` and `res.<requester>` ([06](06-jobs.md)).
A storage node of the original Mosaic idea would follow `storage.<region>.<shard>` for the shards it hosts.

## Patterns
`literal`, `*` = exactly one segment, `>` = one or more trailing segments (last position only).
`storage.*.shard7`, `storage.>`, `>`.

## Algorithm (`match/matcher.h`)
Subscriptions get dense ids. For each pattern position we keep bitsets: `literal[pos][segment]`, `star[pos]`;
plus `exact[len]` (patterns without `>` of that length) and `tail[k]` (patterns with k leading segments then `>`).
Matching a channel of m segments:
```
acc = exact[m] | tail[0..m-1]                 // length filter
for i in 0..m-1:
    open |= tail[i]                           // '>' patterns accept anything from their tail position on
    acc &= open | star[i] | literal[i][seg_i] // patterns allowed at this position
result = set bits of acc
```
Cost O(m × subscribers/64): no per-subscriber work, and independent of how many subscribers do not match.
Correctness is tested against a naive matcher on random patterns, including removals and id reuse.
### Measured cost (MATCH-1, re-measured for MATCH-4)

The whitepaper's "~2 µs/message" claim was inherited aspiration; it is wrong. Measured with
`./build/bench/MosaicTests -tc="*matcher cost*"` (Release build, 2000 matches per case, mixed literal/star/tail patterns). The
*first* column pair is what the optimiser replaced; the second is after it.

| subscriptions | `matches_any()` before | `matches_any()` after | `match()` after |
|---|---|---|---|
| 1,000 | 4.8 µs | **0.19 µs** | 0.34 µs |
| 10,000 | 21.0 µs | **0.61 µs** | 1.23 µs |
| 100,000 | 179.7 µs | **4.2 µs** | 7.6 µs |

The "before" figures above are from an unoptimised build in the original measurement; on the same Release build the optimised
path is 25x faster at 1k and 43x at 100k, because what it removed was an allocation per call plus a bitset copy per segment -
costs that scale with subscribers and were paid on every arriving record.

The growth is **linear in subscriptions**, which is what the algorithm predicts: cost is O(segments × subscribers/64), so
100k subscriptions is ~1,500 words per position, several positions per match. What the design guarantees - that cost does not
depend on how many subscribers *do not* match - holds; the absolute number is simply not 2 µs.

`matches_any()` skips materialising the hit list, which is worth a little at every size (MATCH-1). MATCH-4 then removed the
per-call allocation: `acc_`, `open_`, `allowed_` and the split segment list are reused across calls, so a steady-state match
allocates nothing, and `Bits::and_with` no longer shrinks-then-grows the buffers (that thrash measured *slower* than the copy it
replaced, which is why the first attempt at this was reverted). At 100k subscriptions the cost is now the word passes themselves,
which is the algorithm's floor.

## How nodes "listen only to what they follow"
1. `Node::follow(pattern)` registers the pattern in its own `Matcher` and calls `Transport::subscribe(pattern)`.
2. The transport must not deliver `publish()`ed messages for channels no local pattern matches.
   - `SimTransport`: the network matches the channel against each peer's `Matcher`.
   - **ZeroMQ transport (ZMQ-1):** SUB sockets filter by *byte prefix of the first frame*. Use the channel as the first
     frame and subscribe to the **literal prefix** of each pattern up to its first wildcard (`storage.*.shard7` →
     `storage.`; `>` → `""`). That is a coarse pre-filter done by ZeroMQ in the sender (PUB side filters). The transport
     then applies `Matcher` for the exact decision before handing the message to Node. Add a test for each prefix case.
3. `Node` re-checks `matches_any(channel)` on every record/head, so a misbehaving transport cannot make it store unrelated logs.

## Open work
MATCH-1 benchmark, MATCH-2 list patterns for Hello, MATCH-3 cleanup. Matching is on the channel name only (default taken in [decisions](decisions.md)); add per-field matching only when a use-case needs it.
