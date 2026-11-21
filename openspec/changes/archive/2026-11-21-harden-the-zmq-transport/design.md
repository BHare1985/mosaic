# Design

## Context

See `proposal.md` - Why. What the audit established, in the order the fixes follow:

- `ZmqTransport::poll()` builds `zmq_pollitem_t items[2 + 1]` and appends `1 + subs.size()` entries: memory-safety bug at three
  peers, and gossip now adds peers at runtime. The conformance pair (two transports) never reached the limit.
- Inbound paths read a fixed frame count and never ask `ZMQ_RCVMORE` whether more arrived: a surplus frame stays queued and the
  *next* `poll()` decodes it. The ZAP handler already drains correctly (`RCVMORE` loop), so the pattern exists in the file.
- `apply_limits()` sets `SNDHWM`/`RCVHWM` (default 1000) and `LINGER` 0. There is no `ZMQ_MAXMSGSIZE`, so a frame is buffered
  before the codec's `kMaxFrameBytes` check can refuse it.
- No `ZMQ_ROUTER_HANDOVER`, `ZMQ_IMMEDIATE`, `ZMQ_HEARTBEAT_*` or `ZMQ_TCP_KEEPALIVE`.
- `curve_supported()` creates and terminates a `zmq_ctx` on every call, and is called from `curve_active()` for every socket
  wired.
- `mosaic_transport_new` enables CURVE only when both key strings are non-null (documented as "NULL disables encryption"), and
  `bindings/python/module.c` passes NULL - so Python signs with real keys and ships plaintext. `mosaic_keys` has no accessor for
  the CURVE pair, which is why the binding could not have passed it.
- The pinned libzmq (built from source, `build/all/_deps/libzmq-src`) defines `ZMQ_MAXMSGSIZE`, `ZMQ_IMMEDIATE`,
  `ZMQ_ROUTER_HANDOVER` and `ZMQ_HEARTBEAT_IVL`, so every option below is available.

## Goals / Non-Goals

**Goals**

- No fixed-capacity assumption anywhere in the transport.
- The frame model (atomic multipart) enforced rather than hoped for.
- Memory per peer bounded by a number an operator can compute.
- A dead or restarted peer handled by the transport, not by the kernel's timeouts.
- Encryption the default on every path that can open a socket, including the C ABI and Python.

**Non-Goals**

- Replacing one `SUB` + `DEALER` per peer with a shared socket or a proxy: per-peer sockets are what give identity, per-peer
  HWM isolation and a stable routing id, and the alternative (a single `ROUTER` for replies) would let us answer peers we never
  wired - a different design, not a bug fix.
- `ipc://`/`inproc://` support: endpoint strings travel on the wire in `PeersMsg`, so they are TCP by protocol, not by accident.
- `ZMQ_ROUTER_MANDATORY`: we never send on the `ROUTER`.
- Changing the wire format or the protocol version.

## Decisions

### D1 - A vector, sized from the sockets

`std::vector<zmq_pollitem_t> items(1 + impl_->subs.size())`, filled as before. Rejected: a fixed maximum (the peer set is not
bounded by anything the transport may assume) and a member buffer reused across calls (a `poll()` that adds a peer mid-call would
have to reallocate anyway - the vector is the same cost as the single allocation already happening per call).

The regression test is the point: wire more peers than the old array held, poll from each, and let the sanitizer configuration
prove the fix. A test that only checks "messages still arrive" would have passed before the fix too.

### D2 - Frame counts come from the message kind, and surplus frames are drained

A unicast message is exactly `[identity][payload]`; a live message exactly `[channel][sender][payload]`. After reading the
expected frames, read with `ZMQ_RCVMORE` until it clears: a surplus is a protocol violation. The message is refused, counted in a
new `malformed()` counter, and - when the sender is known - fed to the existing decode-fault path so peer scoring sees it rather
than silently losing it (SEC-2).

Rejected: reading "until RCVMORE clears" as the message body (it would accept a `REQ`-shaped message whose extra frame shifts
every field) and ignoring surplus frames (which is the current desync bug).

### D3 - `ZMQ_MAXMSGSIZE` is the frame limit, set before bind/connect

`ZMQ_MAXMSGSIZE = kMaxFrameBytes` (1 MiB, the codec's own hard limit). It must be applied before `bind`/`connect`, which means in
`Socket` construction rather than in `apply_limits` after the fact. The decoder keeps its check as well: the socket bound protects
memory, the decoder bound protects correctness.

### D4 - The queue limit is the protocol's own in-flight bound

Default `SNDHWM`/`RCVHWM` become **256**, matching `NodeConfig::fetch_window` - the number of records the protocol permits in
flight from one peer. The worst case is then a number an operator can multiply: 256 × 256 KiB = **64 MiB per peer**, stated in
the config comment and in `05-node-and-transport.md`. Rejected: leaving 1000 (a gigabyte per peer at the frame limit) and setting
HWM to 0 (unbounded, which is the bug being fixed).

### D5 - Handover and immediate, because a restart is the common case

`ZMQ_ROUTER_HANDOVER = 1`: a peer that restarts with its own identity while its old connection is still dying otherwise has its
new connection silently refused until the old one expires. `ZMQ_IMMEDIATE = 1` on client sockets: sends to a peer that has not
completed its connection are dropped and counted instead of queued into a socket that may never connect. Together they make "the
peer is connected" mean what the code assumes.

### D6 - Liveness is a socket option, not a hope

`ZMQ_HEARTBEAT_IVL = 5000`, `ZMQ_HEARTBEAT_TIMEOUT = 15000` and `ZMQ_TCP_KEEPALIVE = 1` on every socket that connects or binds
over TCP. The protocol tolerates loss, so a dead link is degraded-not-wrong - but without these, a half-open link keeps a
per-peer queue alive indefinitely. Both are configurable (`heartbeat_ivl_ms`, `heartbeat_timeout_ms`; zero disables), because a
loopback test and a satellite link do not want the same timers.

Each option is applied through a helper that counts a failure instead of ignoring it: an older libzmq that does not know an
option then degrades visibly (`option_rejects` in the counters) rather than silently running without it.

### D7 - The CURVE probe is a function-local static

`static const bool supported = probe_curve_once();` - one `zmq_ctx` per process, thread-safe initialisation, no context churn per
socket. The probe stays a probe (create a `DEALER`, try to set a CURVE key, close) because that is the only honest test of a
build flag.

### D8 - Encryption is the default, and plaintext has a name

The C ABI grows `mosaic_keys_curve(keys, &public_z85, &secret_z85)` (freed with the existing `mosaic_free_string`) - the keystore
already derives the pair (`curve_identity_from_keystore`), and the binding had no way to reach it. `mosaic_transport_new` keeps
its signature but **NULL keys become an error** (`MOSAIC_ERR_INVALID_ARGUMENT`), and a new
`mosaic_transport_new_plaintext(...)` keeps the old behaviour under a name that says what it does. The Python binding loads the
keystore it is already given, derives the pair, and encrypts; `plaintext=True` is the documented diagnostic opt-out.

Rejected: adding `curve` to `mosaic_peer` and inferring encryption from whether peers were given - it makes the secure case
depend on a detail of the *peer* list; and breaking the `mosaic_transport_new` signature outright - it is a 0.x API, but a
mis-call should fail loudly rather than stop compiling.

### D9 - The scale limits get written down rather than discovered

One `SUB` + `DEALER` pair per peer, one thread, one `zmq_poll` over all of them, and a 5 ms quiet-loop pace: the design point is
dozens of peers per node (edge sites, service meshes), not thousands of public clients. `05-node-and-transport.md` states that,
with the numbers behind it, so the limitation is a known property instead of a production surprise.

## Risks / Trade-offs

- **IMMEDIATE changes behaviour for peers that are slow to connect** → Sends are dropped instead of queued. That matches the
  transport contract (best effort) and is counted, so it is visible; the alternative is the queue-into-nowhere behaviour being
  fixed.
- **Heartbeats interact badly with a peer that is merely idle but healthy** → `HEARTBEAT_TIMEOUT` is three intervals and both are
  configurable; a peer that is idle still answers pings, because heartbeats are a ZMTP-level mechanism, not our protocol's.
- **`MAXMSGSIZE` set too tight would refuse legitimate records** → It is derived from the codec's own `kMaxFrameBytes`, so the
  socket and the decoder cannot disagree, and a test asserts a record at the limit still passes.
- **The C ABI's NULL-keys change breaks an existing caller** → It is the *intent* of the change: plaintext by omission is the
  bug. The new plaintext entry point keeps the diagnostic path available, and the header says so.
- **Option support varies with the libzmq a consumer links** → Every `setsockopt` result is counted; a build missing an option
  reports itself instead of pretending.

## Migration Plan

No wire or data migration. The C ABI's NULL-keys behaviour tightens (a caller must now name plaintext); the Python binding becomes
encrypted by default, which needs a keystore path it already accepts. After merge: `./scripts/ci.sh`, then the three-daemon
gossip run again as a smoke test, since that is the topology most likely to exercise the new poll set.
