# Design

## Context

See proposal.md - Why. Constraints that shape this:

- D3 in `docs/design/decisions.md` already chose socket layout **B** (PUB/SUB live + ROUTER/DEALER unicast) because credit and
  peer identity need addressing. This design implements that decision rather than revisiting it.
- The `Transport` contract is transport-agnostic and already has a conformance suite after
  `harden-simulation-fidelity`: `self/subscribe/publish/send/poll`, best effort, no self-delivery, filtered publish.
- `MOSAIC_BUILD_ZMQ_STACK=OFF` currently means "only build the core library and tests"; the current CMake builds
  libzmq/czmq/malamute from source under `all/`.
- Records and messages have bytes only after `verifiable-records-and-wire-format`. This change is written to consume that
  framing and MUST NOT invent its own.
- Everything is single-threaded and clock-injected; `zmq_poll` must fit a pump loop, not a thread per socket.

## Goals / Non-Goals

**Goals:**

- Two processes on different hosts replicating with real bytes, real durability and real link authentication.
- Keep the core library dependency-free: ZeroMQ only in a separate target.
- Make an operator able to see it work with two commands.

**Non-Goals:**

- No gossip discovery, no LAN beacon, no rendezvous server (DISC-2/3). Static configuration is the whole of discovery here,
  and the requirement says so out loud so no one assumes otherwise.
- No ACK/min-replica retention (ACK-1), no offline-writer head exchange (HEADS-1), no credit on the live path (FLOW-3/4).
  Those become urgent only once the writer is expected to go offline, and are the next change.
- No daemon (`mosaicd`), no service units, no packaging. The CLI proves the transport; an operational daemon is a separate
  deliverable.
- No threads. One I/O loop owns every socket; a threaded transport is a later performance decision with its own concurrency
  review.

## Decisions

**D1 - The transport is one object owning both sockets and one poll set.**
`ZmqTransport` binds a PUB and a ROUTER (or connects its SUBs and DEALERs) and exposes the same five contract operations.
*Alternative:* two transport objects (live and repair) - rejected: the node would have to know which socket to use, and the
contract would stop being transport-agnostic.

**D2 - Pattern subscription is translated to a literal prefix, and the local matcher stays authoritative.**
`storage.*.shard7` subscribes with the bytes `storage.`; `>` subscribes with an empty prefix (subscribe to everything). The node
re-checks the full pattern on arrival, exactly as it does under the simulator.
*Why:* ZeroMQ matches byte prefixes only; a naive translation would let `storage.eu.frankfurt.shard7` through a two-segment
pattern. *Alternative:* no transport-level filtering (subscribe to everything, filter locally) - rejected: it hands the network
unbounded irrelevant traffic and defeats the point of pattern subscriptions.

**D3 - Repair addressing uses the node's CURVE public key as identity.**
The ROUTER connection identity is set to the node id, so `send(peer_id, ...)` maps directly onto ZeroMQ routing and onto the
CURVE key in one step. No separate peer directory is needed.
*Alternative:* random per-session routing ids plus a mapping table - rejected: an extra table that can go stale, and it
contradicts the identity-equals-key rule the crypto change established.

**D4 - Queue limits are set explicitly on both sides, tuned so repair traffic is never collaterally dropped.**
Explicit HWM on PUB and on the DEALER side; live overflow is expected and counted, and the follower recovers by fetch. Repair
is already credit-gated so it stays well below the limit.
*Alternative:* default HWM - rejected: it differs per version and turns a capacity decision into an accident.

**D5 - SQLite is the durable store, one table keyed by (writer, channel, sequence).**
WAL mode, one transaction per append, so "success" means committed. Reopen reads the max sequence per log; a torn tail is
impossible with WAL's atomic commit, so recovery is "open and read".
*Alternative:* LMDB or segment files - STORE-2 (segment file) stays on the board; SQLite is chosen first because correctness of
the durability contract matters more than append throughput here, and the conformance suite makes replacing it cheap.

**D6 - The build splits targets rather than options.**
`Mosaic` (core, no ZeroMQ), `MosaicZmq` (transport, links libzmq), `MosaicTests`, and `Mosaic` standalone CLI linking both.
Find system libzmq first; fall back to the existing from-source build in `all/`. The option is renamed to
`MOSAIC_BUILD_ZMQ_FROM_SOURCE`, which is what it has always done.
*Why:* "does the core still build with no ZeroMQ installed" is a property, and properties are easier to keep than options.

**D6a - One identity: Ed25519 signs records, CURVE derives from it (`docs/design/07-security.md`).**
The 2024 `key-loading` branch (branch `key-loading`, commit `a3e7758`) used czmq `zcert` as the source of truth, which
generates an independent curve25519 pair - a second identity with no link to the writer that signs records. Instead: the
keystore holds the Ed25519 pair, `crypto_sign_ed25519_sk_to_curve25519` derives the CURVE key, `sodium_bin2z85` produces the
40-character strings libzmq wants, and the ecosystem certificate format becomes import/export only.
*Alternative:* keep zcert authoritative - rejected, it contradicts D3's "identity agrees between link and log" and leaves two
unrelated keys to manage, rotate and revoke.

**D7 - The CLI is a thin driver over the node, not a second implementation.**
`pub` and `tail` construct a node, drive `pump`/`tick` in a loop with the real clock and a poll timeout, and do nothing else.
Any behaviour the CLI needs (a pure replica, a stream of delivered records) is added to the node as a requirement, not
duplicated in the tool.
*Alternative:* a CLI that talks to the node's internals directly - rejected: it would test a different code path than the library
user gets.

## Risks / Trade-offs

- **Most of this is untestable in the fast loop** → every new requirement gets a conformance or loopback test that runs in the
  full build, and the core-only build is asserted to still pass, so the slow path cannot rot silently.
- **CURVE keys and signing keys coupled** (D3) → rotation (SEC-3) will have to rotate both; recorded as an explicit sequel
  rather than a surprise. Coupling is still preferred because split identities would let an authenticated peer serve records
  attributed to someone else.
- **Two-process tests are flaky by nature** (ports, timing) → tests bind to ephemeral ports, use real timeouts with generous
  bounds, and prefer "converged within N seconds" over fixed sleeps; the CLI demo stays a documented manual check.
- **SQLite becomes a hard dependency of the durable path** → behind `LogStore` and validated by the same conformance suite, so
  the memory store remains the default for tests and an alternative backend is a local change.
- **Big change** (six board tasks, three new capabilities) → tasks are ordered so each step is independently shippable:
  transport before store, store before CLI. If it stalls, splitting at that boundary is a clean cut.
- **PUB-side filtering surprises** (ZeroMQ filters at the publisher, not the subscriber) → the prefix rule is tested for each
  wildcard case explicitly, and the case that motivated it (star does not match a deeper channel) is a named scenario.

## Migration Plan

1. CMake split and target rename; core build without ZeroMQ asserted. Nothing else changes.
2. `ZmqTransport` for loopback with no CURVE; run the conformance suite over TCP.
3. CURVE plus allow-list; conformance again with authentication on.
4. SQLite store plus the durability scenarios; node resumes against it.
5. Static discovery wiring; two-process convergence test.
6. `Mosaic pub` / `Mosaic tail`; documented manual two-host check.

Each step leaves the board in a describable state; rollback is "revert one step", and steps 1-3 do not touch storage.

## Open Questions

- Whether the CLI should grow `verify` and `keygen` subcommands here or in a follow-up. Deferrable: both are additive and
  `verify` needs only the store that lands in step 4.
- Whether `MosaicTests` should run the two-process scenario by default or behind a flag, given CI cost. Deferrable once the
  runtime of the scenario is measured.
