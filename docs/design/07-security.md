# 07 — Security (⬜ placeholders only; read before using in anger)

_Describes 0.6.0._

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
3. **Authorization** (implemented, writes only): which writer may write which channel. `ChannelRules` maps a channel pattern to the
   writer identities allowed there, enforced on ingest before storage and on publish before the log is extended, so a writer
   cannot create history its readers would reject. Open by default - a channel without a rule accepts any verified writer - and
   closing `svc.>` is one line, which is what makes the job layer safe to expose: without it, any key holder could publish a
   request and have a worker run it. Rejections are counted per rule, and a forbidden log is remembered so its head announcements
   do not make us fetch records we would refuse. Rules are configuration per node today; a replicated membership channel is the
   follow-up, and it needs key rotation (SEC-3) before revocation means anything.

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

## What an authenticated peer may cost (implemented)

Encryption and pinning decide *who* may connect; they say nothing about how much a connected peer may ask for. The bounds live
in `NodeConfig` and are enforced before anything is stored or executed: a payload above `max_record_bytes` is refused on ingest
and on publish, a head request carrying more than `max_head_patterns` patterns is refused whole (no matcher is built for it),
per-peer request budgets throttle with a counter rather than a disconnect, and a worker's pending job set is bounded. Exceeding a
budget is visible (`oversized_records`, `head_requests_refused`, `requests_throttled`, `pending_overflow`), never silent.

## What a peer pays for misbehaving (SEC-2, implemented)

The bounds above say how much a peer *may* ask for. They do not make being wrong expensive: a peer that sends garbage is
counted and then served at full rate again on the next tick. Scoring adds the price. Every peer carries a score in `peer_links_`
(one table, one expiry sweep, nothing extra to prune), and only objective faults earn strikes - wrong bytes, never absence:

| Fault (`PeerFault`) | Weight | Why that weight |
|---|---|---|
| `Undecodable` - a frame that does not decode | 1 | usually a bug or a version skew, and cheap to check |
| `Unverified` - signature or hash chain does not verify | 3 | a broken writer or an attacker; either way it must stop quickly |
| `Oversized` - payload above `max_record_bytes` | 2 | refusable from the length alone, so it is deliberate |
| `Unrequested` - a repair record nobody asked for | 2 | volunteering repair traffic is never legitimate |
| `VersionMismatch` - a hello speaking another protocol | 1 | usually a deployment mistake, not an attack |

Strikes **decay continuously** (`strikes -= strikes * elapsed / strike_decay_ms`), so a peer that is merely unlucky never
accumulates and one that floods does. There is no per-peer history: a sliding window of counters would need memory that grows
with the network, and memory an attacker can grow.

**Thresholds and durations** (defaults in `NodeConfig`, settable in the daemon config): `strike_threshold = 8`,
`strike_decay_ms = 60000` (one minute), `ban_base_ms = 5000`, `ban_max_ms = 600000` (ten minutes). Crossing the threshold
bans for the base duration; every further crossing at least doubles it (`base * 2^(bans-1)`, clamped to the maximum), so a
persistent attacker is cheap to hold off and a flapping peer stays recoverable. `strike_threshold = 0` disables banning
entirely while still counting strikes.

What a ban does: the peer's messages are dropped **at the single inbound choke point, before anything is decoded, verified or
stored**, it is not served repair, it is not asked for heads, and it is dropped from job ranking. What a ban does not do: it
never changes acceptance. It keeps the peer's addresses and its place in `peer_links_`, so a lapse needs no restart, and a
banned peer's messages are dropped by the transport too (before decoding) when the transport can be told - see
`Transport::set_peer_ignored`.

A ban ends three ways and every edge is printable (`bans_started`, `peer_bans`, `banned_drops`, `strikes_by_kind` are the
counters; `mosaicd` logs one line per edge and reports the counters on `SIGUSR1`):
1. **It lapses** at its deadline, swept by `tick()` with no operator action.
2. **It is lifted early** when the peer completes a handshake and sends one record that verifies. While banned, exactly two
   things get through - a `HelloMsg` speaking our version, and (after that hello) one record. If the record verifies the ban
   is over and strikes reset to zero; if it does not, the strike re-bans it at double the duration. The reason this is safe is
   in the rule below: the only thing a ban can cost is that peer's own service.
3. **It escalates** if the peer faults again once it is back, which is the doubling case above.

### Bans are not belief

No acceptance decision may depend on a ban. `ReplicaLog::ingest`, signature verification and channel rules know nothing about
scoring, and a record that arrives from a banned peer **by another link** is accepted on its signature like any other. The
temptation is real - "this peer is banned, so its records must be bad" - and it is exactly the reasoning that lets an attacker
censor a node by getting its *neighbour* banned. A ban is local policy about how much attention a peer gets, never a claim
about what is true; the signature is the only thing that decides that.

## Key rotation, and what it cannot do (✅ SEC-3)

A writer hands its log to a successor by publishing a rotation link in its own log (`mosaic/rotation`); a channel rule may name a
*root* and thereby authorize that root and its bounded successors. The mechanism is described in [01](01-data-model.md), and its
security properties are worth stating as limits:

- **It prevents a dead end, not a compromise.** If the old key leaks, the attacker can sign a rotation to their own key. Rotation
  cannot tell "the owner rotating" from "the thief rotating", and a system that claimed otherwise would be lying. What it buys is
  the ability to retire a key *without* breaking every follower's continuity - and a link, once published, is replicated
  everywhere and visible to anyone.
- **Trust does not propagate.** A link is believed only when it lives in the log of an identity the verifier already trusts, and
  only for as many hops as it is configured to follow. A stranger's log claiming to succeed a trusted writer grants nothing.
- **A rotation is not an authorization.** Rules still decide who may write; rotation only changes *which* identity a rule that
  names a root covers. A chain authorizes successors and never predecessors, so rotating away from a key does retire it.
- **Nothing about verification changes.** A record's writer is whoever signed it, always; rotation never rewrites history and a
  fork in either log is reported exactly as before.

## Threat model (first draft; owner: maintainer to review)
| Actor | Can | Cannot / mitigation |
|---|---|---|
| Malicious replica | withhold or delay records, serve stale heads | forge records (signatures); other replicas serve the data |
| Malicious writer | equivocate, spam | hide equivocation from replicas that compare (fork detection); request budgets and bans (SEC-2) |
| Malicious follower | flood FETCH, send garbage | bounded by `max_serve`, credits, per-peer budgets; a banned sender is dropped before decoding (SEC-2) |
| Network attacker | drop/duplicate/reorder, observe | protocol tolerates loss; CURVE hides content |
| Resource exhaustion | huge records, huge `count`, many logs | max sizes in decoder (ENC-1), `max_serve`, per-peer budgets |

## The encryption gap the C ABI had (closed, ZMQ-2)

Worth recording because it was invisible and complete: `mosaic_transport_new` treated *missing* CURVE keys as "plaintext,
quietly", and the Python binding passed NULL - so a Python node signed records with a real Ed25519 key and shipped them over an
unencrypted link, while the C++ daemon and CLI encrypted. The C surface had no way to read the CURVE pair out of a keystore,
which is why the binding could not have done better.

Now: the ABI exposes `mosaic_keys_curve` (the curve25519 pair derived from the identity), a transport built through it is
**encrypted unless the caller asks otherwise**, and `mosaic_transport_new_plaintext` exists so that "unencrypted" is a decision
with a name rather than the behaviour of a forgotten argument. The Python binding loads the keystore it is already given, and
`mosaic.curve_public(path)` gives a peer the key it needs to authenticate you. A test asserts that a transport built without key
material is refused rather than downgraded.

## Rules for code
- Never act on unverified data. Never log payloads. Treat every decoded field as hostile (bounds, sizes).
- Never make acceptance depend on a peer's score ("bans are not belief"). Score where the fault is already counted, and drop
  at `Node::admit()` - the one inbound choke point - so no new message kind can bypass a ban.
- Keys never in config files or logs; keystore API (CORE-2). Rotation = new log + link record (SEC-3).
