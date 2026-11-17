# Proposal

## Why

A node learns its peers from a configuration file and nothing else. That was the right first step (DISC-1) - it made "no
coordinator" concrete - but it means every deployment is hand-wired: adding a third node to a LAN is an edit on all three. The
board has carried DISC-2 (LAN beacon) and DISC-3 (gossip) since the beginning, and both are exactly the kind of distributed
plumbing that is easy to get wrong and expensive to debug.

czmq already ships tested implementations of both, as actors: `zbeacon` announces and listens on a UDP port, `zgossip` spreads a
peer list between nodes. Reusing them is the same trade this project just took for its config file - use the ecosystem's
implementation where it is maintained and fits, rather than owning a second one.

## What Changes

- A peer-discovery port the daemon can be configured with: static list (today's behaviour, unchanged), LAN beacon, gossip, or both.
- Discovery feeds the existing transport through the same pinning path as configured peers: a discovered peer is still subject to
  CURVE and the key policy, so discovery never grants trust - it only says "there may be a node here".
- The actors' threads are confined: they own their own thread, and everything they learn crosses into the main loop as messages on
  an inproc socket that the node's poll already drains. The node stays single-threaded and lock-free.
- Duplicate and stale peers are handled: a peer is dialled once, and a peer that stops announcing is dropped after a timeout.
- **Not** in scope: a rendezvous service (that is a coordination point, and would contradict the design), authenticated discovery
  (CURVE decides who a peer is, not the beacon), and multi-cluster federation.

## Capabilities

### Modified Capabilities

- `discovery/peer-discovery`: peers may be discovered without configuration, and discovered peers are hints subject to the same
  key policy as configured ones.

## Impact

- `include/mosaic/net/peer_discovery.h` (a port), `source/net/discovery_czmq.cpp` (beacon + gossip behind it), daemon config
  (`discover` section), and the transport's `add_peer` path.
- czmq gains a second use in the CLI/daemon targets (the first was `zconfig`), so the dependency policy recorded by
  `mosaicd-daemon` covers it: `core`..`node` untouched, threads allowed only inside these actors.
- Tests: the marshalling is tested in-process with a stub source; the beacon itself needs two processes and is a documented manual
  check, because a UDP broadcast in CI proves nothing about a LAN.
- `docs/design/00-overview.md`, `conventions.md`: the thread exemption, stated once.
- `docs/design/TODO.md`: DISC-2 and DISC-3 close; DISC-1's static path stays the default.
