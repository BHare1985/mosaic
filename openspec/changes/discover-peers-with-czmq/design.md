# Design

## Context

See proposal.md. The node is single-threaded by construction: time is injected, the loop is `pump`/`tick`, and the whole test
strategy rests on that (a scenario is deterministic because nothing runs concurrently). `Transport::peers()` already tells the
node who it can address, `add_peer` already wires a peer discovered late, and the key policy already decides whether a peer may
connect. czmq's `zbeacon` and `zgossip` are both actors - they own a thread each.

## Goals / Non-Goals

**Goals:** peers without configuration; discovery that grants nothing; the node loop still single-threaded and lock-free.

**Non-Goals:** a rendezvous server, authenticated discovery, cluster federation, discovering *services* (that is the jobs layer's
question), or replacing the static list.

## Decisions

**D1 - The thread boundary is a socket, not shared state.**
Each czmq actor writes what it learns to an inproc socket that the node's poll loop drains, exactly like a network peer would. No
mutex, no atomics, no shared map: the actors own their threads and the node owns its loop, and the only thing crossing is a
message. This is the whole reason the thread exemption is safe, and it is what the design set will say in one sentence.
*Alternative:* a background thread calling into the node - rejected: it would make every future behaviour need locking, and the
deterministic tests are the project's main asset.

**D2 - Discovery produces candidates; the key policy produces trust.**
A discovered peer is fed through the same path as a configured one: dial, negotiate, then pass or fail the pinning decision. A
discovered key is *pinned on first sight* in open mode, which is the intended reading of "open" - and refused in closed mode.
*Alternative:* trusting the beacon's identity claim - rejected: a UDP announcement is trivially forged, and CURVE already solves
this correctly.

**D3 - `zbeacon` for the LAN, `zgossip` for a peer list.**
Beacon answers "who is on my network"; gossip answers "who does my network know". They are complementary and both exist in czmq,
so both are used rather than inventing a discovery protocol. Gossip spreads identities and endpoints only - never keys, never
records.

**D4 - A deployment name scopes the beacon.**
The beacon service string includes a configured cluster name, so two deployments on one LAN do not merge. *Trade-off:* a
mis-configured name means a silent no-discovery, so it is echoed at startup like the key rules are.

**D5 - Expiry is the node's job, not the actor's.**
Tracked peers carry a last-seen timestamp and are dropped after a configured timeout, in the node's loop. The actor is a sensor;
deciding what is stale is policy and belongs with the code that dials.

## Risks / Trade-offs

- **Two threads appear in a process that had none** -> confined behind one port, crossing only via an inproc socket, and the
  thread-exemption is written into `conventions.md` with the marshalling rule, so the next person does not repeat it casually.
- **A LAN beacon is a broadcast: anyone can see the deployment exists** -> documented in `07-security.md`: discovery reveals
  presence, CURVE protects content, and a deployment that cannot tolerate that uses the static list.
- **Gossip can amplify** -> bounded peer set, rate-limited accepted entries, and the existing per-peer budgets still apply to the
  resulting connections.
- **A UDP test in CI proves nothing about a real LAN** -> the marshalling and expiry are tested in-process with a stub source; the
  beacon is verified by hand on two hosts and documented as such, rather than faked in a test that looks like evidence.

## Open Questions

- Whether gossip should carry load hints (a node that is nearly full advertising that fact). Deferrable: it overlaps the
  capacity-aware ranking that JOBS-4 wants, and belongs with it.
- Whether the daemon should allow discovery to be enabled per pattern rather than globally. Deferrable: global first.
