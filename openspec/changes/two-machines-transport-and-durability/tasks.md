# Tasks

## 1. Split the build so the core stays dependency-free (ZMQ-0)

- [ ] 1.1 Create `MosaicZmq` as a separate target containing the ZeroMQ transport and discovery wiring; keep `Mosaic` free of
      ZeroMQ. Verified by: building `Mosaic` and `MosaicTests` on a machine with no ZeroMQ installed succeeds.
- [ ] 1.2 Find system libzmq before falling back to building from source, and rename the option to
      `MOSAIC_BUILD_ZMQ_FROM_SOURCE`. Verified by: both paths configure and build; the old option name is gone from docs and
      CMake (`rg -n "MOSAIC_BUILD_ZMQ_STACK" .` returns only intentional migration notes).
- [ ] 1.3 Assert the confinement rule in the build: no ZeroMQ header may be included outside the transport source. Verified
      by: a grep check in the build or a review note; `rg -n "<zmq" include source --glob '!source/net/zmq*'` returns nothing.

## 2. ZeroMQ transport, loopback first (ZMQ-1)

- [ ] 2.1 Implement `ZmqTransport` with PUB for live publication and ROUTER/DEALER for unicast, one poll set, non-blocking
      poll returning at most one envelope. Verified by: the shared transport conformance suite passes against a loopback
      implementation.
- [ ] 2.2 Map each pattern to its literal prefix up to the first wildcard, including `>` mapping to an empty prefix, and keep
      the local matcher authoritative. Verified by: a test per wildcard case, including that `storage.*.shard7` does not admit
      `storage.eu.frankfurt.shard7`.
- [ ] 2.3 Set explicit queue limits on publication and on unicast, and count live-path overflow as a drop. Verified by: a test
      where a stalled subscriber does not block the publisher and the drop counter increases.
- [ ] 2.4 Prove two processes converge over TCP without authentication. Verified by: a two-process test reaching the writer's
      head and surviving a link break that is later restored.

## 3. Link authentication (SEC-1)

- [ ] 3.1 Enable CURVE on both socket kinds with the node identity as the key, and refuse peers absent from the allow-list.
      Verified by: a test where an unlisted key cannot connect and one where a listed key connects and replicates.
- [ ] 3.2 Assert identity agreement: the key that authenticates a link is the key that signed the records it delivers.
      Verified by: a test rejecting a peer whose delivered record carries a different writer identity.
- [ ] 3.3 Document the key distribution assumption (who holds whose public key) in `docs/design/07-security.md`. Verified by:
      the section names the trust assumption and what it does not protect against.

## 4. Durable store (STORE-1)

- [ ] 4.1 Implement a SQLite `LogStore` (one table keyed by writer, channel and sequence, WAL, one transaction per append).
      Verified by: it passes the store conformance suite that `MemoryLogStore` passes.
- [ ] 4.2 Commit before reporting success, and resume at head plus one after reopen. Verified by: a test that appends, kills
      the process, reopens and asserts both the head and the next successful append.
- [ ] 4.3 Recover from a damaged tail to the last complete record without failing to open. Verified by: a test that truncates
      the tail and asserts open succeeds, the head is the last complete record, and earlier records still verify.
- [ ] 4.4 Record the append throughput benchmark in the store README with the reproducible command. Verified by: the numbers
      and command are present and re-runnable.

## 5. Static Peer Discovery (DISC-1)

- [ ] 5.1 Wire the configured peer list into the transport so nodes connect from configuration only. Verified by: two nodes
      configured with each other's identity and endpoint exchange traffic.
- [ ] 5.2 Keep operation non-fatal when a peer is absent or a peer disappears. Verified by: a three-peer test where one peer is
      never started and the other two still replicate.

## 6. Operator surface (CLI-1, NODE-3)

- [ ] 6.1 Add a pure-replica mode to the node: follow with no application callback, store and serve. Verified by: a test where
      a follower converges from that node alone while the writer is offline.
- [ ] 6.2 Add `Mosaic pub`: publish records from stdin or a file to a channel, reporting sequence and hash, rejecting invalid
      channels. Verified by: publishing three records reports sequences 0, 1, 2 and a wildcard channel is refused.
- [ ] 6.3 Add `Mosaic tail`: follow patterns on a remote node, print each matching record exactly once in sequence order, and
      exit successfully on interrupt. Verified by: a late-joining tail prints recovered records before live ones, and one record
      per line with no duplicates.
- [ ] 6.4 Document the two-host demonstration in `README.md` (publish on A, tail on B) and verify it manually once. Verified
      by: the documented commands work as written on two processes.

## 7. Close the board and prove the milestone

- [ ] 7.1 Mark ZMQ-0, ZMQ-1, DISC-1, SEC-1, STORE-1, CLI-1 and NODE-3 done; update the M3 milestone row.
      Verified by: `./scripts/check_todo_ids.sh` exits 0 and no `TODO(ZMQ-*|DISC-1|SEC-1|STORE-1|CLI-1)` remains in code.
- [ ] 7.2 Update the README status banner to state what is now true (two processes, durable, CURVE, no broker) and what is
      still not (no ACK-based retention, no gossip discovery, no jobs runtime). Verified by: the banner matches the shipped
      capabilities, with each claim traceable to a spec.
- [ ] 7.3 Run the definition of done with the ZeroMQ stack both from source and from the system: builds warning-free,
      conformance suites green, clang-format clean, single-threaded invariants intact, no ZeroMQ header outside the transport.
      Verified by: both build paths green and the review checklist satisfied.

## Workflow follow-up

- The resilience change (ACK-1, HEADS-1, FLOW-3, FLOW-4) consumes this transport; it should not need to modify it, only to add
  messages already reserved in the wire format.
- If the SQLite dependency is later rejected, the store conformance suite makes a segment-file backend a drop-in replacement;
  only step 4 is affected.
