# Proposal

## Why

A review of the transport against ZeroMQ's own model turned up seven things that are wrong or fragile, and one of them is a
memory-safety bug: `ZmqTransport::poll()` keeps its poll set in `zmq_pollitem_t items[2 + 1]` and appends one entry per wired
peer, so **a node with three or more peers writes past the end of a stack array** and hands `zmq_poll` a count it cannot use.
The conformance tests only ever pair two nodes, so nothing caught it - and peer-list gossip now adds peers at runtime, which makes
the array easier to overflow than it was yesterday.

The rest are the footguns this design is otherwise careful about, each cheap to close once named: a message with more frames than
its kind declares desynchronises the *next* message; there is no `ZMQ_MAXMSGSIZE`, so an oversized frame is buffered before the
codec refuses it; the queue limit is counted in messages, not bytes, so the worst case per peer is a gigabyte; a peer that
restarts with its old identity still connected is silently refused; a link that dies silently is noticed only when TCP gives up;
and the CURVE support probe builds and tears down a whole `zmq_ctx` per socket.

Separately, and worse: **the C ABI and therefore the Python binding run unencrypted by default.** `mosaic_transport_new` treats
NULL CURVE keys as "plaintext, quietly", and `bindings/python/module.c` passes NULL. A Python node signs its records with a real
Ed25519 key and ships them over a plaintext link, which contradicts the transport's own requirement that every connection be
CURVE-encrypted.

## What Changes

- **The poll set is sized from the sockets it watches** (a `std::vector`), so adding peers cannot corrupt memory. A regression
  test wires more peers than the old array could hold and runs in the sanitizer configuration.
- **Frame counts are enforced, not assumed.** A ROUTER message is exactly two frames and a live message exactly three; anything
  else is drained to the end, counted, and reported as a decode fault against its sender, so scoring sees it.
- **Bytes are bounded at the socket.** `ZMQ_MAXMSGSIZE` refuses an oversized frame before libzmq buffers it; the queue limit
  drops to the protocol's own in-flight bound (256, matching `fetch_window`) with the worst-case per-peer footprint documented
  as a number.
- **Links are supervised.** Heartbeats and TCP keepalive replace "wait for the kernel"; `ZMQ_ROUTER_HANDOVER` lets a restarted
  peer take over its old identity, and `ZMQ_IMMEDIATE` stops sends queueing into a peer that has not connected.
- **The CURVE probe is computed once per process**, not once per socket.
- **Encryption becomes the default on the C/Python path**: the C ABI gains a way to read CURVE keys out of a keystore, plaintext
  becomes an explicit request rather than the behaviour of a missing argument, and the Python binding loads the keystore it is
  already given and encrypts with it.
- **The scale limits are written down**, so nobody discovers the per-peer socket model in production: a mesh of dozens is the
  design point; thousands of peers per node is not.

Not in scope: replacing the per-peer `SUB`/`DEALER` model with a shared socket, adding a proxy, transport independence
(`ipc://`/`inproc://`), or changing the wire format. **Not breaking** for the C++ API; the C ABI gains functions and keeps the
existing one (its meaning tightens, which the header states).

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `net/zeromq-transport`: adds requirements that the poll set is sized for its peers, that a message is exactly the frames its
  kind declares, that frame size and queue depth are bounded in bytes, that a silent link is detected and a restarted peer is
  admitted, and that a connection attempt which has not connected does not queue.
- `api/c-abi`: adds the requirement that a transport built through the ABI is encrypted unless the caller explicitly asks for
  plaintext, and that the C surface can obtain the CURVE material from a keystore so a binding has no excuse to skip it.

## Impact

- `source/net/zmq_transport.cpp` (poll set, frame discipline, socket options, probe caching), `include/mosaic/net/zmq_transport.h`
  (counters, config knobs).
- `include/mosaic/mosaic_c.h`, `source/capi/mosaic_c.cpp` (key accessors, explicit plaintext constructor),
  `bindings/python/module.c` (encrypt by default).
- Tests: `test/source/test_transport_zmq.cpp` (poll depth, frame discipline, options), the C ABI test, the Python demo.
- Docs: `docs/design/05-node-and-transport.md` (socket options and their reasons, the scale limits), `docs/design/07-security.md`
  (the plaintext default that this closes).
