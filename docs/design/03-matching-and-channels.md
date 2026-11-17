# 03 — Matching & channels (✅ matcher implemented; ZeroMQ mapping ⬜)

_Describes 0.6.0._

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
### Measured cost (MATCH-1)

The whitepaper's "~2 µs/message" claim was inherited aspiration; it is wrong. Measured on the reference machine
(`./build/test/MosaicTests -tc="*matcher cost*"`, 2000 matches per case, mixed literal/star/tail patterns):

| subscriptions | `match()` (id list) | `matches_any()` (per message) |
|---|---|---|
| 1,000 | 6.7 µs | 4.8 µs |
| 10,000 | 22.7 µs | 21.0 µs |
| 100,000 | 191.7 µs | 179.7 µs |

The growth is **linear in subscriptions**, which is what the algorithm predicts: cost is O(segments × subscribers/64), so
100k subscriptions is ~1,500 words per position, several positions per match. What the design guarantees - that cost does not
depend on how many subscribers *do not* match - holds; the absolute number is simply not 2 µs.

`matches_any()` skips materialising the hit list, which is worth ~6% at 100k and ~29% at 1k. The dominant cost is the bitset
work itself: each position builds a temporary `allowed` bitset by copying `open`. Removing those temporaries (a reused scratch
buffer) and scanning word-wise with an early exit are the obvious next steps; they are recorded as MATCH-4 rather than done here,
because this change's job was to measure first.

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
