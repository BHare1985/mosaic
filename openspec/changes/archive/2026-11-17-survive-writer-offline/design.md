# Design

## Context

See proposal.md. Relevant state: single-writer signed logs with repair (`FetchMsg`/`DirectRecordMsg`) already converging under
30% loss; a credit window on the repair path only; `AckMsg`, `GetHeadsMsg`, `DirectHeadMsg` and `CreditMsg` defined with
permanent wire ids but no behaviour; SQLite and memory stores both behind `LogStore`; everything single-threaded and
clock-injected, which is what makes these policies testable.

## Goals / Non-Goals

**Goals:** a record that the writer considers safe is on at least `min_replicas` replicas; a log remains discoverable and
replicable with its writer gone; a follower cannot be flooded into unbounded memory; every new behaviour has a sim scenario.

**Non-Goals:** consensus, leader election, exactly-once semantics, cross-log ordering, automatic membership. Also not: choosing
`min_replicas` for anyone - the default stays zero and the deployment decides.

## Decisions

**D1 - Acknowledgements carry a sequence, not a promise about the writer's history.**
`AckMsg{key, seq}` means "I have this log up to seq, durably". The writer keeps `acked[peer] = seq` and treats retention as
satisfied when the count of replicas with `acked >= n` reaches `min_replicas`. *Alternative:* per-record acks - rejected: one
message per record per replica is a lot of chatter for information the highest sequence already implies.

**D2 - Retention is applied by the writer, not by the store.**
The writer decides what it may stop tracking; the store keeps everything unless a future retention backend says otherwise
(STORE-3). This keeps `LogStore` dumb and avoids pushing policy into every backend.

**D3 - The retention bound is reported, never enforced silently.**
When `min_replicas` cannot be met within the configured limit, the writer releases the oldest records and increments a
visible counter. Blocking forever would turn an unreachable replica into an outage; staying silent would turn it into data loss
nobody noticed.

**D4 - Head exchange is a request over the normal transport, not a new discovery mechanism.**
`GetHeadsMsg{patterns}` is unicast to peers; each answers `DirectHeadMsg` for what it holds. This reuses the transport,
including its ZMQ implementation, and needs no rendezvous service. *Alternative:* a periodic gossip broadcast - rejected for
now: the request form is bounded and answers exactly the question a newcomer has.

**D5 - A peer's head is only believed for logs it answers for.**
A `DirectHeadMsg` is a hint like any other head announcement: it triggers repair, and the records are still verified. A
malicious peer can waste a fetch but cannot corrupt a log, so no extra authentication is needed.

**D6 - The slow-follower threshold is measured in sequences behind, not bytes or time.**
`behind = known_head - next_seq`. It is cheap, deterministic, and expressible in a sim scenario without reasoning about socket
buffers. *Trade-off:* it ignores record size, which is acceptable while records are bounded by the wire format's limit.

**D7 - Credit on the live path is opt-in.**
A receiver that grants credit gets paced delivery; one that never grants keeps today's best effort. This is what lets the
mechanism land without changing every existing deployment's behaviour.

## Risks / Trade-offs

- **Retention hides loss** -> the shortfall counter and the per-log list are part of the contract, and a test asserts a scenario
  where the floor is never met.
- **Acks inflate on reconnect** -> acks are idempotent and take the max, so a reconnecting replica cannot regress or multiply a
  count.
- **Head exchange becomes a request flood** -> requests are rate-limited to one per peer per interval, like the hello retry, and
  a responder caps its reply at `max_serve` logs.
- **Slow-follower switching could thrash** -> switching back to live delivery requires the gap to close, with hysteresis handled
  by the same threshold and tested with a follower that catches up once.

## Open Questions

- Whether retention needs a store-side `truncate_below` in this change or waits for STORE-3. Deferrable: the writer can retain
  in memory and leave the store append-only.
- Whether `min_replicas` should be settable per channel pattern as well as per log. Deferrable: per log first.
