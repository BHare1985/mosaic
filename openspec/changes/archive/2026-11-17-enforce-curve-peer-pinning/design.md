# Design

## Context

See proposal.md. Constraints that shape this: the transport is single-threaded and clock-free; `PeerPins` already implements
both policies and is unit-tested; libzmq offers ZAP as the only pre-message hook for CURVE; and the transport creates its own
`zmq_ctx`, so a handler can bind inside that context without coordinating with anything else.

## Goals / Non-Goals

**Goals:** refuse the wrong key before a single message is decoded; keep one decision point (`PeerPins`); keep the loop
single-threaded.

**Non-Goals:** key discovery or distribution; authenticating the *record* writer (already independent); TLS-like domain
separation.

## Decisions

**D1 - ZAP served from `poll()`, not a thread.**
libzmq blocks the incoming handshake until the handler answers. Serving that from the transport's own poll loop keeps the
project's single-threaded rule and needs no synchronisation: `poll()` first services ZAP requests (non-blocking), then ordinary
traffic. *Alternative:* a dedicated handler thread - rejected: it introduces the first lock in `core`..`node` and a second
lifetime to reason about, for a task that is a few microseconds per connection.

**D2 - The decision is `PeerPins::observe`, unchanged.**
The handler extracts the client's public key from the ZAP request, z85-encodes it, and asks the same policy object the tests
already cover. No second policy implementation, no behaviour that tests do not already describe.

**D3 - Identity for pinning is the peer's routing identity.**
ZAP gives the address and the ZMTP identity; the identity is the node id we configured, so pins stay keyed by the node the
operator named, not by a transport detail. *Trade-off:* a peer that presents a fresh identity is a new pin in open mode - which
is exactly what "open" means, and closed mode refuses it.

**D4 - The message-level check stays as a second line.**
After ZAP, an encrypted message always has an authenticated peer, so the check becomes cheap and consistent; keeping it means a
transport misconfiguration cannot silently disable enforcement.

## Risks / Trade-offs

- **A blocking handshake served by a slow loop** -> `poll()` is called every iteration by the node's pump loop; the interval is
  milliseconds, which is slower than ideal for first contact but never stalls data already flowing.
- **Operators surprised by closed mode** -> the failure is visible (`curve_rejects`) and logged in the CLI demo documentation.
- **ZAP domain semantics** -> we accept any domain; the identity check is what matters here.

## Implementation findings

The first attempt was reverted on a wrong conclusion; the second found the real cause and D1 stands.

- **D1 is valid.** Serving ZAP from `poll()` works. The earlier "in-loop servicing does not deliver traffic" observation was a
  symptom of the malformed reply below, not of the servicing model: a minimal libzmq repro now delivers in both modes.
- **The reply is six frames**, matching czmq's `zauth`: version, request id, status code, status text, user id, metadata. REP
  prepends the request's envelope, so an explicit empty delimiter frame shifts every index and libzmq reports
  `ZAP_MALFORMED_REPLY` - which looks exactly like "the peer never connected".
- **The handshake does not expose the peer's identity**, so pinning cannot be keyed on a configured node id. `PeerPins::decide`
  therefore keys open mode on the peer address (pin on first sight, refuse a change) and closed mode on the presented key
  (it must match a provisioned entry). Both are unit-tested.

## Open Questions

- Whether open mode should ever *re-pin* (trust on second use) - no, that would defeat pinning; not revisiting.
- **Blocking:** how to service ZAP without breaking the single-threaded invariant, given that in-loop servicing did not deliver
  traffic. Options: a dedicated handler thread (needs a written justification and a concurrency review); poll the ZAP socket with
  a blocking timeout inside a small service loop during startup only; or revisit whether libzmq is being driven correctly
  (for example whether the reply must be written before the IO thread's next poll tick).
