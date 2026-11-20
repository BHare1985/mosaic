# 05 — Node & Transport

_Describes 0.6.0._

## Node (✅ `include/mosaic/node/node.h`)
One class, three roles (writer, replica, responder). Configuration (`NodeConfig`): `head_interval_ms`, `fetch_batch`,
`fetch_window`, `fetch_timeout_ms`, `max_serve`. Driving loop (owned by the app/CLI):
```cpp
for (;;) { now = clock_ms(); node.pump(now); node.tick(now); wait_for_socket_or_timer(); }
```
Everything is single-threaded and clock-injected. State: `writers_`, `remote_head_`, `fetches_`, `peer_credits_`, `following_`,
and one `peer_links_` table holding each peer's version *and* its score (SEC-2), so there is one lifetime rule and one expiry
sweep. Planned: ACK/HEADS/HELLO pattern advertisement.
Peer scoring - strikes, decay, escalating bans, and the rule that a ban never changes what is accepted - is in
[07](07-security.md).

## Transport contract (✅ `net/transport.h`)
`self()`, `subscribe(pattern)`, `publish(channel, msg)`, `send(to, msg)`, `poll()`, `peers()`, plus two optional capabilities with
no-op defaults: `take_decode_faults()` (frames that did not decode, so `Node` can score the sender - SEC-2) and
`set_peer_ignored(peer, bool)` (drop a banned peer's frames *before* decoding, which is the cheap half of a ban; a transport that
ignores the hint is still correct, because `Node::admit()` drops them anyway).
Best effort, non-blocking, no self-delivery, publish filtered by subscription, unicast to unknown peers is silently dropped.
`peers()` is a best-effort snapshot of addressable peers (never self): it is what the version handshake iterates over, and it is
also where per-link credit and CURVE identities will hang in ZMQ-1.

The conformance suite is `run_transport_conformance(factory)` in `test/source/transport_conformance.h` (NET-0). It takes a
factory that builds two connected endpoints plus a `settle()` step, and asserts the whole contract without knowing which
transport it is testing. **A new transport is accepted by adding one test case that calls it** - that is the gate for ZMQ-1,
and the suite must never gain a transport-specific branch.

## SimNetwork (✅)
Fully connected and fully seeded, but delivery is **scheduled**, not immediate: a send picks an arrival time and
`advance(now_ms)` releases what is due. That is what makes latency, jitter-induced reordering, timeouts and partitions
expressible in simulated time with no sleeping (SIM-1):

- `set_loss`, `set_duplicate`, `set_delay(min, max)` apply to every link; `set_link` overrides one direction.
- Each link **direction** owns a generator stream derived from `(seed, from, to)`, so adding a peer does not change the
  faults an existing link suffers, and a reported seed still reproduces a failure.
- `partition(a, b)` / `heal(a, b)` drop silently, exactly like loss, so a node cannot tell them apart.
- `reordered()` counts deliveries that arrived out of send order, so a test asserts the fault model did something
  instead of assuming it.

Scenarios are named functions over a seed, run by `test/source/scenario.h`: `first_failing_seed(n, scenario)` reports the
first failing seed, and `MOSAIC_SEEDS` raises the sweep size (SIM-2). A convergence claim must cite a sweep, not one run.
**All new Node behaviour must come with a sim scenario.**

## ZeroMQ transport (⬜ ZMQ-0, ZMQ-1; layout **B** chosen, see [decisions](decisions.md) D3)
Two layouts were considered; the `Transport` interface hides the choice, so Node does not change:

| Layout | Live path | Repair/unicast | Pros | Cons |
|---|---|---|---|---|
| **A. Dafka-shaped** | PUB/SUB, first frame = channel | also PUB/SUB, first frame = target node id | one socket kind, matches Dafka wire shape, trivially brokerless | unicast over PUB/SUB drops silently, no peer identity/back-pressure signal |
| **B. Split** (**chosen**) | PUB/SUB, first frame = channel | ROUTER/DEALER (ROUTER identity = NodeId) | real unicast, identities for credits and ACL, back-pressure visible | two sockets per peer link, slightly more code |

Whichever layout: bind a PUB (and ROUTER) per node; connect SUBs/DEALERs to discovered peers; use CURVE (SEC-1); set HWM; apply the
prefix-subscription rule from [03](03-matching-and-channels.md); one I/O thread, poll loop owning all sockets
(`zmq_poll`), feeding `poll()`.

## Peer-list gossip (✅ DISC-3, implemented in `Node`)

Three ways to learn an address, in the order they were built: a static list, a LAN beacon, and gossip. The beacon only reaches
one link layer, so a deployment without a shared segment needed every node listed everywhere - and removing one node from such
a list disconnected whoever learned of each other through it.

Gossip is a protocol message rather than a socket concern: `PeersMsg` (id 10) carries bounded entries of
`(id, pub endpoint, router endpoint, hops, age)`, is encoded by `proto`, carried by `Transport` and handled by `Node`. That is
why it needs no new thread, no new socket type and no czmq - which was the blocker for `zgossip`
([upstream-notes](../upstream-notes.md#finding-zgossip-can-be-commanded-but-not-listened-to-from-outside-czmq)).

- **Push, with ages.** Every `gossip_interval_ms` (default 5 s) a node offers its freshest entries to `gossip_fanout`
  neighbours (default 2). Epidemic spread with fanout 2 covers a network in O(log N) rounds, and one small message per interval
  costs less than the head announcements already flowing.
- **A node advertises itself** (`hops = 0`), so its address does not depend on a third party staying alive. Without that, every
  entry would die with its only introducer.
- **`hops` is a distance, not a countdown** (capped at `gossip_max_hops`, default 6): a nearer entry describes a peer better
  than one six hops away that may be quoting a stale address, so a closer sighting wins.
- **`age` is staleness, not longevity**: the sender reports how long since anything confirmed the entry, so a node that has known
  a peer for an hour still emits a fresh entry. Entries that nothing refreshes expire after `gossip_entry_ttl_ms` (default 30 s).
- **The table is bounded** (`gossip_table_limit`, default 256) by recency, and an entry that is stale, too far, malformed,
  oversized or about ourselves is refused and counted. A peer list over the per-message cap is refused **whole** with one strike
  - the same fault the wire decoder reports for the same bytes.
- **A hint, never authority.** `Node` never dials: `take_new_gossip_peers()` hands candidates to the caller (the daemon), which
  goes through `ZmqTransport::add_peer` - so CURVE and `PeerPins` decide as they do for any other peer. A peer the config file
  placed is never re-dialled at a gossiped address (`set_configured_peers`).

Counter to watch: `mosaicd`'s `discovered` counts peers learned this way. Verified end to end by three daemons where the
third knows only the first and still reaches the second.

## Discovery (✅ DISC-1..3)
Four ways to find a peer, none of them a single point of failure (unlike Dafka's tower): the static list in the config, the LAN
beacon (`ZmqBeaconDiscovery`, czmq's `zbeacon` on its own thread, its findings crossing into the node loop as values), peer-list
gossip above, and a rendezvous server if an operator ever wants one - it would be the same `PeerDiscovery` shape as the others.
`PeerBook` is the shared bookkeeping: dial a peer once, remember it, notice one that goes quiet before forgetting it.
