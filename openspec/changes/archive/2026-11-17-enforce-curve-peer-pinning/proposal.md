# Proposal

## Why

`PeerPins` holds the policy for which key a peer may present (open: pin on first use; closed: explicit list), but the transport
cannot consult it at the right moment. libzmq does not expose the authenticated peer key as message metadata, so today a server
accepts *any* client key and only counts that it could not check (`curve_unauthenticated`). That means a peer can claim another
node's identity to the server, which is the one impersonation hole left in an otherwise authenticated system.

This change moves the decision to the handshake, where the peer's key is actually known, using ZeroMQ's ZAP mechanism.

## What Changes

- A ZAP handler serving the CURVE handshake, consulted before any message is exchanged, so an unlisted or changed key is refused
  at connect time rather than counted after the fact.
- The handler is polled from the transport's existing loop, not a thread of its own, so the single-threaded invariant holds.
- `PeerPins` becomes the single decision point for both policies; the transport stops counting `curve_unauthenticated` on
  encrypted links (it can now answer the question instead of noting that it could not).
- **Not** in scope: discovering peers, distributing keys, or deciding who *should* be in a deployment. This enforces whatever
  the operator configured.

## Capabilities

### Modified Capabilities

- `net/zeromq-transport`: peer-key pinning becomes enforced at handshake time instead of "pinned where observable".

## Impact

- `include/mosaic/net/zmq_transport.h`, `source/net/zmq_transport.cpp`: ZAP socket, handshake decision, refresh of the counters.
- `test/source/test_transport_zmq.cpp`: the impersonation test that was withdrawn when the metadata route failed can come back.
- `docs/design/07-security.md` and the board: SEC-1b closes; `SEC-1` becomes fully done.
