# Tasks

## 1. Split the build so the core stays dependency-free (ZMQ-0)

- [x] 1.1 Create `MosaicZmq` as a separate target containing the ZeroMQ transport and discovery wiring; keep `Mosaic` free of
      ZeroMQ. Verified by: building `Mosaic` and `MosaicTests` on a machine with no ZeroMQ installed succeeds.
- [x] 1.2 Find system libzmq before falling back to building from source, and rename the option to
      `MOSAIC_BUILD_ZMQ_FROM_SOURCE`. Verified by: both paths configure and build; the old option name is gone from docs and
      CMake (`rg -n "MOSAIC_BUILD_ZMQ_STACK" .` returns only intentional migration notes).
- [x] 1.3 Assert the confinement rule in the build: no ZeroMQ header may be included outside the transport source. Verified
      by: a grep check in the build or a review note; `rg -n "<zmq" include source --glob '!source/net/zmq*'` returns nothing.

- [x] 1.4 Give the fast loop and the `all/` build separate output directories. Today both write
      `build/test/MosaicTests`, so whichever built last silently wins: a "full stack" run can execute the core-only binary
      and report a green that never touched ZeroMQ. Verified by: building both in one session and getting two distinct
      binaries with distinct case counts (the `all/` build includes the Malamute compile check).
- [x] 1.5 Decide and record how libsodium reaches a consumer of the packaged library. It is currently linked
      `PRIVATE $<BUILD_INTERFACE:sodium>` to keep it out of `install(EXPORT)`; that is right for building Mosaic and wrong for
      anyone consuming it. Verified by: either the export carries sodium as a deliberate requirement, or the CMake comment and
      `docs/design/00-overview.md` say plainly that consumers must supply it.

## 2. ZeroMQ transport, loopback first (ZMQ-1)

- [x] 2.1 Implement `ZmqTransport` with PUB for live publication and ROUTER/DEALER for unicast, one poll set, non-blocking
      poll returning at most one envelope. Verified by: the shared transport conformance suite passes against a loopback
      implementation.
- [x] 2.2 Map each pattern to its literal prefix up to the first wildcard, including `>` mapping to an empty prefix, and keep
      the local matcher authoritative. Verified by: a test per wildcard case, including that `storage.*.shard7` does not admit
      `storage.eu.frankfurt.shard7`.
- [x] 2.3 Set explicit queue limits on publication and on unicast, and count live-path overflow as a drop. Verified by: a test
      where a stalled subscriber does not block the publisher and the drop counter increases.
- [ ] 2.4 Prove two processes converge over TCP without authentication. Verified by: a two-process test reaching the writer's
      head and surviving a link break that is later restored.

## 3. Link authentication (SEC-1)

- [ ] 3.1 Enable CURVE on both socket kinds with the node identity as the key, and implement the two policies: open with
      per-endpoint key pinning (default), and closed with an explicit allow-list. **CURVE is on for every link and verified**
      (`transport conformance: ZeroMQ with CURVE`, plus a wrong-server-key test); `PeerPins` implements both policies and is
      unit-tested. Server-side enforcement of the *client's* key is not possible in this libzmq - it does not expose the peer
      key as message metadata - so it moves to SEC-1b (a ZAP handler) and `curve_unauthenticated` counts the gap. Open until
      that lands, because "pinned" would otherwise be a claim the code cannot keep.
- [ ] 3.2 Assert identity agreement: the key that authenticates a link is the key that signed the records it delivers.
      Half is proven (`the CURVE identity derives from the signing identity` - one keystore entry yields both, and it survives a
      reload). The end-to-end assertion needs the peer key on the wire, i.e. SEC-1b; open until then.
- [x] 3.3 Document the key distribution assumption in `docs/design/07-security.md`: open mode trusts self-verifying data and
      only pins link identity, closed mode trusts the provisioned list. Verified by: the section says what each mode protects
      against and, explicitly, that a link key never authorises a record.

- [ ] 3.4 Derive the CURVE key pair from the Ed25519 identity (`crypto_sign_ed25519_sk_to_curve25519`, then z85), instead of
      generating an independent pair. **Derivation done** (`curve_identity_from_keystore`, `core/z85.h`, RFC 32 vector and
      stability tests); the second half of the verification - two nodes with different keystores cannot impersonate each other -
      needs CURVE on a live link and lands with 3.1/3.2.
- [ ] 3.5 Import and export the ecosystem's certificate container (czmq `zcert` files) for the derived identity, without that
      file becoming the source of truth. This is what lets the surrounding ZeroMQ tooling be used, and it is the piece the
      2024 `key-loading` branch had (czmq `zcert_new`/`zcert_load`/`zcert_save`) - that branch made the file authoritative and
      generated a second, unrelated curve25519 identity, which is the part not to repeat. Verified by: importing a file
      produced by that tooling yields an identity usable for CURVE and for signing, and an exported file is accepted back.

## 4. Durable store (STORE-1)

- [x] 4.0 Extract the store conformance suite before writing a second store (board item STORE-0): turn the inline
      `MemoryLogStore contract` case into `run_store_conformance(factory)` in the style of
      `run_transport_conformance`. Verified by: `store conformance: MemoryLogStore` passes, and the suite covers contiguity,
      refused rewrites, bounded ordered reads, empty/unknown logs, per-writer and per-channel isolation, enumeration order and
      the store-never-verifies rule - which the old inline case did not (it had one log and one writer).
- [x] 4.1 Implement a SQLite `LogStore` (one table keyed by writer, channel and sequence, WAL, one transaction per append).
      Verified by: `store conformance: SqliteLogStore` - the same suite `MemoryLogStore` passes, unchanged. Records are stored
      as their wire encoding, so storage and transmission share one byte format. SQLite is fetched as the amalgamation when no
      system copy is installed, which keeps "clone and build" working without a package manager.
- [x] 4.2 Commit before reporting success, and resume at head plus one after reopen. Verified by: a test that appends, kills
      the process, reopens and asserts both the head and the next successful append.
- [x] 4.3 Recover from a damaged tail to the last complete record without failing to open. Verified by: `a damaged tail ends
      the log instead of breaking it` - the last row's blob is corrupted at the byte level, the store still opens, the head
      becomes the last decodable record, and reads stop at the damage rather than serving nonsense.
- [x] 4.4 Record the append throughput benchmark in the store README with the reproducible command. Verified by: 818us per
      durable append (5000 appends), recorded with its command and the explicit note that this is the price of
      synchronous=FULL - group commit is the fix if a writer needs more, and that is a policy decision with this baseline.

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

- [ ] 6.5 Absorb the deferred half of SIM-2: a `Mosaic sim` subcommand that runs a named scenario across a seed sweep and
      prints the failing seed. It was deferred from the harness change because the scenarios live in the test sources; here a
      real command surface exists. Verified by: one documented command runs a sweep and reports the seed count.
- [ ] 6.6 Sweep `clang-format` across the seven files that still carry drift (board item CI-4: `core/record.h`,
      `core/types.h`, `proto/messages.h`, `log/log.cpp`, `match/matcher.cpp`, `store/memory_log_store.cpp`,
      `test/malamute.cpp`). Cheap while the tree is already being touched. Verified by: `clang-format --dry-run --Werror` clean
      on every tracked source.

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
