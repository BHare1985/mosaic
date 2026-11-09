# Building a useful decentralized broker in 2026

A design guide for Mosaic, distilled from the ZeroMQ Guide, Dafka and
Malamute (see [references](references/README.md)) and adjusted for today.
Section 1 states what we borrow; sections 2-9 are the design; section 10 is the plan.

## 1. What to borrow, and from where

| Idea | Source | Take / leave |
|---|---|---|
| Single-writer partitions: each producer owns its log | Dafka README "Producer" | **Take.** It removes the need for consensus to replicate. |
| Producer sends direct to consumers *and* stores; HEAD heartbeats expose gaps; FETCH repairs them | Dafka README "Design", `dafka_proto.xml` | **Take.** Fast path is plain pub/sub; repair is request/reply. |
| Consumers own their offsets; cluster does not track them | Dafka | **Take.** Consumers cost the cluster nothing. |
| No consumer groups / no consensus on join | Dafka "Design" | **Take**, document the trade-off (no automatic work-sharing; see 8). |
| Tower: rendezvous only, no data path | Dafka | **Leave as a concept**; replace with pluggable discovery (4). |
| LevelDB store | Dafka `dafka_store_*.c` | **Leave.** Use a storage interface (5). |
| Protocol defined in XML, code generated (zproto) | Dafka/Malamute `*_proto.xml` | **Take the idea**: one machine-readable protocol definition; choose a modern generator. |
| Credit-based flow control per client | Malamute `STREAM.md` | **Take** for slow-consumer handling instead of silent HWM drops. |
| Subject/address pattern matching, mailboxes, services | Malamute | **Optional later** (layer above the log). |
| Clone pattern: snapshot + updates, sequence numbers, state recovery | Guide ch. 5 | **Take** for catch-up semantics. |
| Heartbeating, idempotent services, Binary Star, Freelance | Guide ch. 4 | **Take** heartbeats; Freelance = brokerless client reliability model. |
| Unprotocols, state machines, tests via simulation | Guide ch. 7-8 | **Take** the method: small specs, explicit state machines, deterministic tests. |

## 2. Principles

1. **The log is the product.** Everything else (queues, KV, chain, tokens) is a
   view over logs.
2. **One writer per log, many replicators.** Ordering is trivial; identity is a
   key pair.
3. **Verify, don't trust.** Every record is signed and hash-chained; replicas
   and consumers can check without trusting the sender.
4. **Brokerless data path, pluggable control path.** Data flows peer-to-peer;
   discovery is replaceable.
5. **Small spec first** (an unprotocol, 1-2 pages), versioned from the first byte.
6. **Everything testable without a network** (inproc transport, simulated
   clock, fault injection).

## 3. Data model

* **Identity:** an Ed25519 key pair per writer; `writer_id` = public key.
* **Log:** `(writer_id, topic)` identifies one append-only sequence.
* **Record:**
  ```
  writer_id | topic | seq (u64) | prev_hash | timestamp | content_type | payload | sig
  ```
  `hash = H(all fields except sig)`, `prev_hash` = hash of record `seq-1`,
  `sig = Sign(hash)`. BLAKE3 or SHA-256 for `H`. Dafka has only
  `address/subject/sequence/content`; hashing and signing are the additions.
* **Head:** `(writer_id, topic, seq, hash)`, optionally signed, used for
  heartbeats and as a compact commitment to the whole prefix.
* **Fork detection:** two different records with the same `(writer_id, topic,
  seq)` are proof of equivocation; replicas keep and gossip the evidence.
  (This is the property a future chain layer builds on.)

## 4. Protocol (modelled on Dafka)

Reuse the Dafka vocabulary so the references stay readable:

| Message | Direction | Purpose |
|---|---|---|
| `RECORD` | writer → consumers, stores (pub/sub) | Fast path delivery |
| `HEAD` | writer → all, periodic | "my log ends at seq N / hash H"; gap detection |
| `ACK` | store → writer | Record durably stored; writer may drop its local copy once `min_replicas` ACKs |
| `GET_HEADS` / `DIRECT_HEAD` | consumer ↔ store | New subscriber learns current heads |
| `FETCH` | consumer → store/writer | Request `[seq, seq+count)` |
| `DIRECT_RECORD` | store/writer → consumer | Repair delivery, unicast |
| `HELLO` (consumer/store) | peer → peers | Announce topics, capabilities, protocol version |

Transport mapping (libzmq): `PUB/SUB` (or `XPUB/XSUB`) for RECORD/HEAD;
`ROUTER/DEALER` for FETCH/DIRECT_*/ACK. Two additions over Dafka:

* **Credits** on the repair path (Malamute-style) so a lagging consumer cannot
  flood a store.
* **Capability/version negotiation** in HELLO; unknown message ids are ignored.

Discovery (replaces Dafka's tower): a `Discovery` interface returning
endpoints for a topic. Implementations, in order of effort: static config;
gossip of peer lists over the log protocol itself; mDNS/UDP beacon for LAN
(Guide ch. 8 / Zyre); optional rendezvous server for NAT'd peers.

## 5. Storage

Interface, not a database:

```
append(writer, topic, record) -> ok
read(writer, topic, from_seq, count) -> records
head(writer, topic) -> (seq, hash)
```

Default: SQLite (zero-ops, one file, WAL) or LMDB; append-only segment files
as the optimised option. Retention is per-log: keep-all, keep-last-N, or
snapshot-and-truncate. Truncation must preserve a signed checkpoint
`(seq, hash)` so the chain stays verifiable from the checkpoint.

## 6. Reliability and flow control

Know the ZeroMQ failure modes and design for them explicitly (Guide ch. 4-5):

* PUB/SUB **drops** when a subscriber is slow or late: the high-water mark
  is a feature, so detection must be in-protocol. `HEAD` heartbeats and
  `seq` gaps give detection; `FETCH` gives repair. This is Dafka's core loop.
* Slow-subscriber detection ("Suicidal Snail", Guide ch. 5): consumers that
  fall behind by more than a threshold stop relying on the live stream and
  catch up through `FETCH`, then rejoin live.
* Heartbeats with explicit liveness timeouts on every peer link.
* Idempotent writes: `(writer_id, topic, seq)` is the idempotency key, so
  retransmit is always safe.
* Writer durability: writer keeps unacked records until `min_replicas` ACKs
  (Dafka `dafka_unacked_list`).

## 7. Security

* Transport: libzmq **CURVE** (requires libsodium) with ZAP for
  authorization. Peer allow-lists keyed by public key.
* Object-level: the Ed25519 record signature, independent of transport, so a
  record stays verifiable after it is stored and forwarded.
* Keys: a small keystore API; no keys in config files; rotation = start a new
  log and sign a link record in the old one.
* Threat model doc to be written with the spec: malicious store (can withhold,
  cannot forge), malicious writer (can equivocate, detectable), malicious
  consumer (can request, bounded by credits).

## 8. What this deliberately does not do

* **No global total order.** Only per-writer order. Cross-writer order is the
  consumer's choice (timestamps, vector clocks, or a higher layer).
* **No consumer groups / work-sharing.** Dafka drops them for join speed.
  Provide an optional layer later (e.g. deterministic partition assignment by
  hash of consumer id, no consensus).
* **No consensus.** A chain/consensus layer (the original Mosaic vision)
  consumes these logs as its transport and evidence store.

## 9. Testing

* Protocol as an explicit state machine; unit tests drive it with messages.
* Network layer behind an interface; run whole clusters in one process on
  `inproc://` with a fake clock and injected drop/delay/duplicate/reorder.
  (Dafka's cucumber step runners and Zyre's testing chapter show the style.)
* Property tests: any interleaving of loss/duplication still converges to
  identical, verifiable logs on all replicas.
* Interop: a Malamute-compat build test and, where feasible, a Dafka
  RECORD/HEAD/FETCH conformance test if wire-compat is chosen.
* CI matrix: GCC and Clang current + previous, Linux first; libzmq pinned
  to a release and to master (the "ZeroMQ still builds" track).

## 10. Build plan

| Milestone | Deliverable | Done when |
|---|---|---|
| M0 | Build revived; docs and references (this PR) | tests pass on GCC 15 |
| M1 | Unprotocol spec v0 (1-2 pages) + threat model | reviewed by maintainer |
| M2 | Record format, hash chain, signatures, in-memory store | unit + property tests |
| M3 | Writer / consumer / store over `inproc` with fault injection | convergence tests pass |
| M4 | TCP + CURVE, SQLite store, `mosaicd`, CLI | two machines replicate a log |
| M5 | Discovery (static + gossip + LAN beacon) | no manual endpoint config on a LAN |
| M6 | C ABI + first binding (Python) | `pip install` demo |
| Track D | CI that builds libzmq/czmq/malamute/dafka from pinned refs | weekly job, results published |

## 11. Open risks

* Re-implementing instead of reviving Dafka costs time; mitigated by reusing
  its message set and test ideas. If speed matters more, revisit option B in
  [direction.md](direction.md).
* libzmq's draft APIs (RADIO/DISH, CLIENT/SERVER) are not part of the stable
  API; avoid them to keep portability.
* "Useful in 2026" is a hypothesis; validate with one concrete demo (e.g. two
  laptops syncing a signed event log offline) before building M5+.
