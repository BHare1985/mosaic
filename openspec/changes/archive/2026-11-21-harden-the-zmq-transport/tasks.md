# Tasks

## 1. The memory-safety bug

- [x] 1.1 Replace the fixed poll array in `ZmqTransport::poll()` with a `std::vector<zmq_pollitem_t>` sized from
      `1 + impl_->subs.size()`. Verified by: a test wires more peers than the old array held and polls from each; it fails under
      the sanitizer configuration before the fix and passes after.
- [x] 1.2 Keep the per-peer delivery path working at that depth: every wired peer's message is still delivered, subscriptions
      applied at `wire_peer` time included. Verified by: the same test asserts one message from each of the peers.

## 2. Frame discipline

- [x] 2.1 Enforce the frame count per message kind (unicast = identity + payload, live = channel + sender + payload) and drain
      surplus frames with `ZMQ_RCVMORE` so the next poll starts clean. Verified by: a peer that sends one frame too many has the
      message refused, and its next legitimate message is delivered rather than the surplus frame.
- [x] 2.2 Count refusals in a new `malformed()` counter and report a known sender through the existing decode-fault path, so
      peer scoring sees a protocol violation as it sees a decode failure. Verified by: the counter moves and the sender's strike
      is recorded on the next pump.
- [x] 2.3 A truncated multi-frame message is refused and counted, with nothing delivered. Verified by: the guard is the same
      `malformed` path the surplus-frame test exercises, and it is defensive by nature - libzmq makes a partial multipart
      message undeliverable (a peer that dies mid-message has the whole message discarded), so there is no wire-level test that
      can produce one.

## 3. Bounds in bytes

- [x] 3.1 Set `ZMQ_MAXMSGSIZE` to the wire format's frame limit, applied before bind/connect (so in `Socket` construction, not
      after). Verified by: a frame above the limit is refused by the socket and nothing reaches the decoder, while a record
      exactly at the limit still passes.
- [x] 3.2 Default `SNDHWM`/`RCVHWM` to the protocol's in-flight bound (256, matching `fetch_window`) and state the resulting
      worst-case per-peer footprint (queue limit × frame limit) in the config comment and the design doc. Verified by: the
      documented number matches the defaults, and the existing overflow test still passes with its explicit HWM of 1.
- [x] 3.3 Apply every socket option through a helper that counts a rejection, so a libzmq without an option degrades visibly.
      Verified by: the counters expose the rejections and the transport still runs.

## 4. Link supervision

- [x] 4.1 Set `ZMQ_HEARTBEAT_IVL` (default 5 s), `ZMQ_HEARTBEAT_TIMEOUT` (default 15 s, three intervals) and
      `ZMQ_TCP_KEEPALIVE`, with both tunable from `ZmqConfig` and zero disabling heartbeats. Verified by: a test reads the
      options back with `zmq_getsockopt` and asserts the configured values.
- [x] 4.2 Set `ZMQ_ROUTER_HANDOVER` and `ZMQ_IMMEDIATE` on the sockets that need them. Verified by: a peer that reconnects with
      the identity it already holds takes over and traffic flows again while the old connection is still open, and a send to a
      wired-but-unconnected peer is dropped and counted rather than queued.

## 5. Encryption by default on the C/Python path

- [x] 5.1 Add `mosaic_keys_curve(keys, &public_z85, &secret_z85)` to the C ABI, deriving from the keystore through
      `curve_identity_from_keystore`, with the strings freed by the existing `mosaic_free_string`. Verified by: the C test loads a
      keystore, reads the pair, and builds an encrypted transport from it.
- [x] 5.2 Make NULL keys an error in `mosaic_transport_new` and add `mosaic_transport_new_plaintext` for the explicit case; say
      so in the header comment. Verified by: a C test asserts `MOSAIC_ERR_INVALID_ARGUMENT` for missing keys and that the
      plaintext entry point still opens a socket pair.
- [x] 5.3 Fix `bindings/python/module.c` to derive the CURVE pair from the keystore it already loads and pass it, with
      `plaintext=True` as the documented diagnostic opt-out. Verified by: the Python demo runs two processes over an encrypted
      link, and a Python node built without keys fails instead of running plaintext.
- [x] 5.4 Confirm the existing two-process C pair still runs, now encrypted by default. Verified by: `mosaic_c_pubtail` in the
      full configuration.

## 6. Close out

- [x] 6.1 Write the socket options and their reasons into `docs/design/05-node-and-transport.md`, and state the scale limits
      (one socket pair per peer, one thread, 5 ms pacing; dozens of peers, not thousands) with the numbers behind them. Verified
      by: the doc names every option that is set and the per-peer memory bound.
- [x] 6.2 Record in `docs/design/07-security.md` that the C ABI and Python binding encrypted nothing by default until this
      change, and what now prevents a silent downgrade. Verified by: the section names the fix and the explicit plaintext escape.
- [x] 6.3 Add a board row for this work, closing it when green; `./scripts/check_todo_ids.sh`; `./scripts/ci.sh` green in every
      configuration including sanitizers, docs and the site. Verified by: the check script and the five configurations.
- [x] 6.4 Re-run the three-daemon gossip smoke test, which wires the most peers of any scenario in the tree and is what the
      poll-set fix is for. Verified by: d3 still discovers the peer it can only reach through d1.
