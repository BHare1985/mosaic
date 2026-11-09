# 05 — Node & Transport

## Node (✅ `include/mosaic/node/node.h`)
One class, three roles (writer, replica, responder). Configuration (`NodeConfig`): `head_interval_ms`, `fetch_batch`,
`fetch_window`, `fetch_timeout_ms`, `max_serve`. Driving loop (owned by the app/CLI):
```cpp
for (;;) { now = clock_ms(); node.pump(now); node.tick(now); wait_for_socket_or_timer(); }
```
Everything is single-threaded and clock-injected. State: `writers_`, `remote_head_`, `fetches_`, `peer_credits_`, `following_`.
Planned: metrics (NODE-1), unfollow & batching (NODE-2), store-node preset (NODE-3), ACK/HEADS/HELLO handling.

## Transport contract (✅ `net/transport.h`)
`self()`, `subscribe(pattern)`, `publish(channel, msg)`, `send(to, msg)`, `poll()`.
Best effort, non-blocking, no self-delivery, publish filtered by subscription, unicast to unknown peers is silently dropped.
A conformance suite (NET-0) must test these for every transport.

## SimNetwork (✅)
Fully connected, deterministic, seeded RNG, `set_loss`, `set_duplicate`. Missing: latency, reordering, partitions, per-link
loss (SIM-1); scenario runner with seed sweeps (SIM-2). **All new Node behaviour must come with a sim scenario.**

## ZeroMQ transport (⬜ ZMQ-1, blocked on decision D3)
Two candidate socket layouts; the `Transport` interface hides the choice, so Node does not change:

| Layout | Live path | Repair/unicast | Pros | Cons |
|---|---|---|---|---|
| **A. Dafka-shaped** | PUB/SUB, first frame = channel | also PUB/SUB, first frame = target node id | one socket kind, matches Dafka wire shape, trivially brokerless | unicast over PUB/SUB drops silently, no peer identity/back-pressure signal |
| **B. Split** (recommended) | PUB/SUB, first frame = channel | ROUTER/DEALER (ROUTER identity = NodeId) | real unicast, identities for credits and ACL, back-pressure visible | two sockets per peer link, slightly more code |

Either way: bind a PUB (and ROUTER) per node; connect SUBs/DEALERs to discovered peers; use CURVE (SEC-1); set HWM; apply the
prefix-subscription rule from [03](03-matching-and-channels.md); one I/O thread, poll loop owning all sockets
(`zmq_poll`), feeding `poll()`.

## Discovery (⬜ DISC-1..3)
`Discovery::peers()` today only has `StaticDiscovery`. Order of work: static list wired into the ZeroMQ transport →
gossip of peer lists (as ordinary messages) → LAN UDP beacon → optional rendezvous server. No single point of failure
should be required for operation (unlike Dafka's tower).
