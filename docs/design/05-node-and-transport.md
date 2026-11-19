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

## Discovery (⬜ DISC-1..3)
`Discovery::peers()` today only has `StaticDiscovery`. Order of work: static list wired into the ZeroMQ transport →
gossip of peer lists (as ordinary messages) → LAN UDP beacon → optional rendezvous server. No single point of failure
should be required for operation (unlike Dafka's tower).
