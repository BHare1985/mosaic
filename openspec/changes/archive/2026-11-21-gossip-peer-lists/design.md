# Design

## Context

`Discovery` (in `net`) has a table of candidates fed by `ZmqBeaconDiscovery` (czmq `zbeacon`, its own actor thread) and by static
config. `PeerPins` decides at the ZAP handshake whether a key may connect (open mode pins on first use, closed mode uses a list).
`Node` already sends `HelloMsg` on the head interval and has a per-peer table.

The `zgossip` experiment and its failure are recorded in `docs/upstream-notes.md`: the events arrive as private zproto frames
(`zgossip_msg.h` lives in czmq's `src/`, is not installed), so using it would put czmq inside `net`. Dropped.

## Decisions

### D1 - Gossip in `Node`, not in `net`

The message is a protocol message like any other, so it is encoded in `proto`, carried by `Transport`, and handled by `Node` - which
means it works over the ROUTER/DEALER path with the same CURVE protection, ZAP policy, budgets and scoring as everything else. No new
thread, no new socket type, no czmq.

That also means gossip inherits the ban machinery from `penalize-abusive-peers` for free, which is why that change is listed as
prerequisite rather than parallel.

### D2 - Push entries with ages, do not track versions

Two gossip designs: anti-entropy (exchange digests, pull differences) or epidemic push (send what you know with ages). Digest exchange
is precise and expensive; push is approximate and cheap. Discovery only needs "sooner or later every node hears about every live
node", so:

```cpp
struct PeersMsg {
    std::vector<PeerEntry> entries;   // id, pub endpoint, router endpoint, age_ms
};
```

Each node, every `gossip_interval_ms` (default 5 s, jittered ±20%), picks `min(gossip_fanout, peers.size())` random peers (fanout 2)
and sends its freshest entries. Age is the sender's view, incremented on receipt - the receiver never trusts a timestamp, only a
duration, which is why the spec refuses negative or absurd ages instead of pretending to reconcile clocks.

### D3 - Fanout 2, because it is enough

Epidemic spread with fanout 2 covers a network of size N in O(log N) rounds with high probability; with a 5 s interval that is
sub-minute convergence for thousands of nodes. Fanout 1 is fragile when one neighbour is down; fanout 3+ multiplies traffic for a
property that is already satisfied.

Cost per node is one small message every 5 s. That is less than the head announcement traffic that already exists.

### D4 - Hops as a distance, not a counter

An entry carries `hops` (how far from its origin it has travelled). It is capped at `gossip_max_hops` (default 6) and dropped
beyond. A distance rather than a TTL decremented per hop, because a distance lets the receiver prefer nearer information when two
entries describe the same identity, and it survives aggregation.

### D5 - The table is the beacon's table, bounded and expiring

Entries land in the existing `Discovery` candidate table, so the "discovered peers are hints, never authority" requirement already
covers them: they must pass `PeerPins` at the handshake. The table gets a bound (`max_candidates`, default 256) with LRU eviction
by last-refresh, and entries expire after `entry_ttl_ms` (default 10x the gossip interval) unless refreshed - the same shape the
beacon path already uses.

Configured peers are stored separately from candidates and always win, which is the requirement "gossip cannot rewrite
configuration", not a convention.

### D6 - Oversized gossip is a strike

An entry list over the per-message cap, or an entry with an endpoint that is not a `tcp://` address, is malformed by our own rules.
It counts toward peer scoring: a node that gossips garbage is the node that gets skipped in ranking and repair, which is the
behaviour we want and the reason this change depends on `penalize-abusive-peers`.

## Risks

- **Address poisoning**: a hostile peer invents 256 fake entries, and the node spends its dial budget on them. Contained by the table
  bound, by pinning (a fake peer never completes a handshake and is dropped), by strikes, and by keeping configured peers separate.
- **Gossip storms after a partition heals**: everyone gossips at once. Jitter on the interval and a per-peer rate budget (the existing
  request budget machinery) make the burst proportional to the fanout, not to the network size.
- **Discovery becomes an amplifier for bans**: a peer could gossip a third party into a ban. It cannot: bans are per link, awarded
  for that peer's own frames, never for what it says about someone else.
