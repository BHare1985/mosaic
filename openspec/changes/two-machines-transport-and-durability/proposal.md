# Proposal

## Why

Everything Mosaic claims today is demonstrated inside one process against a simulated network with in-memory storage. The
README promises "a brokerless replicated-log library and daemon" and the direction document's acceptance test is concrete:
publish on one host, tail on another, durable, encrypted. Until that exists the project is a well-specified simulator, and no
operational claim - edge deployment, local-first sync, tamper-evident audit - can be evaluated by anyone.

This change crosses the process boundary: real sockets, real persistence, real peer authentication, and the CLI that lets a
person see it work.

## What Changes

- A ZeroMQ transport implementing the existing transport contract in the chosen split layout: PUB/SUB for live traffic,
  ROUTER/DEALER for repair unicast, addressed by node identity.
- Pattern handling that respects how ZeroMQ actually filters: subscribe by the literal prefix up to the first wildcard, then
  re-check the full pattern locally. Wildcard semantics never depend on the socket layer.
- Explicit high-water marks, so pressure appears as counted drops rather than as silent memory growth or blocked senders.
- CURVE encryption with a peer allow-list: an unlisted key cannot connect, and a node's identity and its encryption key agree.
- A durable store with a documented commit point: an acknowledged append survives a reopen and resumes at the right sequence.
- Static peer discovery: peers come from configuration, and losing any one of them does not stop the rest.
- A CLI with `Mosaic pub` and `Mosaic tail`, plus a build split so the core library still builds and tests with no ZeroMQ
  installed at all.
- **BREAKING** for the transport target only: `Mosaic` (no ZeroMQ) and `MosaicZmq` become separate targets, and the existing
  build option is renamed to say what it actually does.

## Capabilities

### New Capabilities

- `net/zeromq-transport`: the ZeroMQ-specific observable behaviour - socket layout, prefix subscription and local re-check,
  queue-limit drop semantics, CURVE authentication with an allow-list, and confinement of ZeroMQ to one target.
- `discovery/peer-discovery`: how a node learns which peers to connect to, starting with a static list and explicit about not
  being a coordination service.
- `cli/operations`: the operator-facing surface - publish to a channel, tail channel patterns, and the exit/streaming
  behaviour a script or a person relies on.

### Modified Capabilities

- `store/log-store`: adds durability - an append that reports success is committed, a reopen continues the log at the correct
  sequence, and a torn write at the tail cannot make a log unreadable. Requires `establish-capability-baseline` archived first.
- `node/node-runtime`: adds the requirement that a node can run as a pure replica (holding logs without an application
  callback), which is what makes a tail-only CLI process meaningful. Requires the baseline archived first.

## Impact

- New target `MosaicZmq` (`source/net/zmq_transport.cpp`, discovery wiring); `Mosaic` keeps building with no ZeroMQ.
- CMake: split targets, find system libzmq before falling back to building from source, rename `MOSAIC_BUILD_ZMQ_STACK` to
  describe "build the ZeroMQ stack from source".
- `include/mosaic/store/sqlite_log_store.h` + source, plus a store conformance run against it.
- `standalone/`: `Mosaic pub` / `Mosaic tail` subcommands, `standalone/mosaicd` deferred.
- New dependency: **libzmq** (product-level, already implied by the direction), plus libsodium for CURVE (approved in the
  crypto change).
- `docs/design/TODO.md`: ZMQ-0, ZMQ-1, DISC-1, SEC-1, STORE-1, CLI-1 close; NODE-3 closes as a side effect.
- Unblocks the resilience work (ACK-1, HEADS-1, FLOW-3/4, gossip discovery) and the jobs layer, both of which need real peers.
