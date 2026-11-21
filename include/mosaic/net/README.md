# net — transport, discovery, and the simulator

**Purpose:** `Transport` is the only thing `Node` knows about networking. `SimNetwork` / `SimTransport` is a
deterministic in-process network that *schedules* delivery, so loss, duplication, delay, reordering and partitions are all
injectable in simulated time. `Discovery` answers "who are my peers?".

**Files:** `transport.h` (contract!), `sim_network.h`, `peer_discovery.h` (`PeerDiscovery` and `PeerBook`),
`peer_pins.h` (CURVE key policy), `zmq_transport.h`.

**Rules**
- Read the contract comment in `transport.h` before writing a transport: best-effort, non-blocking, single-threaded,
  filtering by subscription, no self-delivery.
- A new transport must pass the transport conformance suite (NET-0) and the same convergence scenario as `SimNetwork`.
- The real ZeroMQ transport (ZMQ-1) is the only code in the project allowed to include ZeroMQ headers in `include/`.
  Everything else stays testable without it.
- Two optional capabilities exist beyond the core contract, both with no-op defaults because a transport that lacks them is
  still correct: `take_decode_faults()` (frames that did not decode, attributed to a sender, so `Node` can score them - SEC-2)
  and `set_peer_ignored()` (drop a banned peer's frames before decoding). `SimTransport` deliberately implements only the first:
  it carries decoded values, so a sim exercises `Node`'s own choke point and its counters.

**Open work:** none from this module's own list. ZMQ-1, DISC-1, DISC-2, DISC-3 and NET-0 are done; gossip lives in `node`
(it is a protocol message, not a socket concern - see `docs/design/05-node-and-transport.md`). The simulator (SIM-1) and the
seed-sweep runner (SIM-2) are done: `test/source/scenario.h` and `test/source/transport_conformance.h`.
