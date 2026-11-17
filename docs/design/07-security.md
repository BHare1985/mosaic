# 07 — Security (⬜ placeholders only; read before using in anger)

> **Records are authenticated; links are not.** CORE-2 landed: records are signed with Ed25519 and the writer identity is the
> hex public key, so a tampered or forged record is rejected. `InsecureTestSigner` still exists as a test double only (its own
> header, definition in the test sources) and provides no security at all. Transport security is still missing: CURVE and the
> peer allow-list are SEC-1, so for now anyone who can reach a socket can talk to it.

## Layers
1. **Record signatures** (CORE-2): Ed25519 via libsodium; `WriterId` = hex public key; sign `record_hash`. Independent of
   transport, so a record stays verifiable after being stored and forwarded by untrusted nodes.
2. **Transport security** (SEC-1): ZeroMQ CURVE, on by default for any real link. Protects confidentiality and lets a client
   prove it is talking to the server whose key it expects.
   **Peer-key enforcement (SEC-1b, done).** The client's key is decided at the handshake by a ZAP handler served from the
   transport's own poll loop, so an unlisted or changed key never completes a connection. Open mode pins on first sight of a
   peer address and refuses a later change; closed mode admits only provisioned keys. Keying on the peer's *identity* is not
   possible: ZeroMQ exchanges identity after the handshake, so the ZAP request's identity frame is empty.
3. **Authorization** (later): which writer may write which channel; which peer may follow which pattern.

## One identity, two representations (decided)

Record signing needs **Ed25519**; ZeroMQ CURVE needs a **curve25519** key pair in **z85** form (`zmq_curve_*`, and ZAP
allow-lists are z85 public keys). The 2024 `key-loading` branch solved this the other way round - czmq's `zcert` was the source
of truth and generated an independent curve25519 pair - which leaves a node with two unrelated identities and no way to say
"the peer that authenticated this link is the writer of this record".

Decision: **Ed25519 is the identity; the CURVE key is derived from it.** `crypto_sign_ed25519_sk_to_curve25519()` converts the
signing key, `sodium_bin2z85()` produces the 40-character strings libzmq wants, so one keystore entry yields both. `zcert` files
stay supported as an **import/export format**, because the rest of the ZeroMQ ecosystem (czmq/malamute tooling) speaks it, and
interoperating with those tools is worth more than the convenience of not writing a converter. That converter belongs to the
transport change (SEC-1), not to this one.

## Threat model (first draft; owner: maintainer to review)
| Actor | Can | Cannot / mitigation |
|---|---|---|
| Malicious replica | withhold or delay records, serve stale heads | forge records (signatures); other replicas serve the data |
| Malicious writer | equivocate, spam | hide equivocation from replicas that compare (fork detection); rate limits (SEC-2) |
| Malicious follower | flood FETCH | bounded by `max_serve`, credits, peer scoring (SEC-2) |
| Network attacker | drop/duplicate/reorder, observe | protocol tolerates loss; CURVE hides content |
| Resource exhaustion | huge records, huge `count`, many logs | max sizes in decoder (ENC-1), `max_serve`, per-peer budgets |

## Rules for code
- Never act on unverified data. Never log payloads. Treat every decoded field as hostile (bounds, sizes).
- Keys never in config files or logs; keystore API (CORE-2). Rotation = new log + link record (SEC-3).
