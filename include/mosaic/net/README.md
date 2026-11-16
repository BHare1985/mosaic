# net — transport, discovery, and the simulator

**Purpose:** `Transport` is the only thing `Node` knows about networking. `SimNetwork` / `SimTransport` is a
deterministic in-process network with loss and duplication. `Discovery` answers "who are my peers?".

**Files:** `transport.h` (contract!), `sim_network.h`, `discovery.h`.

**Rules**
- Read the contract comment in `transport.h` before writing a transport: best-effort, non-blocking, single-threaded,
  filtering by subscription, no self-delivery.
- A new transport must pass the transport conformance suite (NET-0) and the same convergence scenario as `SimNetwork`.
- The real ZeroMQ transport (ZMQ-1) is the only code in the project allowed to include ZeroMQ headers in `include/`.
  Everything else stays testable without it.

**Open work:** SIM-1, SIM-2, NET-0, ZMQ-1, DISC-1..3.
