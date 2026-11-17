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
- [x] 2.4 Prove two processes converge over TCP. Verified by: the same demo, twice - three records delivered live over an
      encrypted link, then a second publisher process resuming the log from its durable store (seq 3, 4) while a restarted
      tail caught up through the repair path rather than losing them.

## 3. Link authentication (SEC-1)

- [x] 3.1 Enable CURVE on both socket kinds with the node identity as the key, and implement the two policies: open with
      per-endpoint key pinning (default), and closed with an explicit allow-list. **CURVE is on for every link and verified**
      (`transport conformance: ZeroMQ with CURVE`, plus a wrong-server-key test); `PeerPins` implements both policies and is
      unit-tested. Server-side enforcement of the *client's* key is not possible in this libzmq - it does not expose the peer
      key as message metadata - so it moves to SEC-1b (a ZAP handler) and `curve_unauthenticated` counts the gap. Open until
      that lands, because "pinned" would otherwise be a claim the code cannot keep.
- [x] 3.2 Assert identity agreement: the key that authenticates a link is the key that signed the records it delivers.
      Half is proven (`the CURVE identity derives from the signing identity` - one keystore entry yields both, and it survives a
      reload). The end-to-end assertion needs the peer key on the wire, i.e. SEC-1b; open until then.
- [x] 3.3 Document the key distribution assumption in `docs/design/07-security.md`: open mode trusts self-verifying data and
      only pins link identity, closed mode trusts the provisioned list. Verified by: the section says what each mode protects
      against and, explicitly, that a link key never authorises a record.

- [x] 3.4 Derive the CURVE key pair from the Ed25519 identity (`crypto_sign_ed25519_sk_to_curve25519`, then z85), instead of
      generating an independent pair. Verified by: `the CURVE identity derives from the signing identity` - one keystore entry
      yields both, stable across a reload, and two identities never produce the same link key. The impersonation half of the
      original verification is server-side key observation, which is SEC-1b (see 3.1).
- [x] 3.5 Import and export the ecosystem's certificate container (czmq `zcert` files) for the derived identity, without that
      file becoming the source of truth. Verified by: a round trip preserves keys and metadata, a file shaped like czmq's output
      (comment header, sections, quoted values, CRLF) loads correctly, and an incomplete certificate is rejected rather than
      half-loaded. `core/zcert.h` is import/export only - the keystore stays authoritative, which is the difference from the
      2024 `key-loading` branch that made the certificate the source of truth and generated a second identity.

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

- [x] 5.1 Wire the configured peer list into the transport so nodes connect from configuration only. Verified by: two nodes
      configured with each other's identity and endpoint exchange traffic.
- [x] 5.2 Keep operation non-fatal when a peer is absent or a peer disappears. Verified by: a three-peer test where one peer is
      never started and the other two still replicate.

## 6. Operator surface (CLI-1, NODE-3)

- [x] 6.1 Add a pure-replica mode to the node: follow with no application callback, store and serve. Verified by: a test where
      a follower converges from that node alone while the writer is offline.
- [x] 6.2 Add `Mosaic pub`: publish records from stdin or a file to a channel, reporting sequence and hash, rejecting invalid
      channels. Verified by: publishing three records reports sequences 0, 1, 2 and a wildcard channel is refused.
- [x] 6.3 Add `Mosaic tail`: follow patterns on a remote node, print each matching record exactly once in sequence order, and
      exit successfully on interrupt. Verified by: a late-joining tail prints its durable history first (seq 0-4), then `ready`,
      then live records - one per line. Two CLI details this needed: replay from the store on start (the node only reports newly
      accepted records, so a restarted tail would otherwise begin mid-history), and a readiness line after the subscriptions
      have had time to propagate.
- [x] 6.4 Document the two-host demonstration in `README.md` (publish on A, tail on B) and verify it manually once. Verified
      by: the documented commands were run here as two processes over real TCP with CURVE, including a publisher restart that
      resumed the log at seq 3 and a tail that replayed history before going live.

- [x] 6.5 Absorb the deferred half of SIM-2: a `Mosaic sim` subcommand that runs a named scenario across a seed sweep and
      prints the failing seed. Verified by: `Mosaic sim --seeds 8 --records 150` reports "8/8 seeds converged under 30% loss,
      10% duplication and jitter" and exits non-zero on a split. It uses real Ed25519 keys, not the test double, so the CLI can
      never link the insecure signer.
- [x] 6.6 Sweep `clang-format` across the files that still carried drift (board item CI-4). Verified by:
      `clang-format --dry-run --Werror` clean on every tracked source in the repository - the first time that has ever been
      true here, because the config itself said `UseTab: Always` until the harness change fixed it.

## 7. Close the board and prove the milestone

- [x] 7.1 Mark ZMQ-0, ZMQ-1, DISC-1, SEC-1, STORE-1, CLI-1 and NODE-3 done; update the M3 milestone row.
      Verified by: `./scripts/check_todo_ids.sh` exits 0 and no `TODO(ZMQ-*|DISC-1|SEC-1|STORE-1|CLI-1)` remains in code.
- [x] 7.2 Update the README status banner to state what is now true (two processes, durable, CURVE, no broker) and what is
      still not (no ACK-based retention, no gossip discovery, no jobs runtime). Verified by: the banner matches the shipped
      capabilities, with each claim traceable to a spec.
- [x] 7.3 Run the definition of done with the ZeroMQ stack both from source and from the system: builds warning-free,
      conformance suites green, clang-format clean, single-threaded invariants intact, no ZeroMQ header outside the transport.
      Verified by: four build combinations green - sodium on/off x ZeroMQ on/off (20909 / 20794 / 20799 / 20748 assertions),
      `rg '<zmq' include source` finds nothing outside the transport source, and no file in the tree is format-dirty.

## Workflow follow-up

- The resilience change (ACK-1, HEADS-1, FLOW-3, FLOW-4) consumes this transport; it should not need to modify it, only to add
  messages already reserved in the wire format.
- If the SQLite dependency is later rejected, the store conformance suite makes a segment-file backend a drop-in replacement;
  only step 4 is affected.
