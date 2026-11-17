# TODO board

_Describes 0.6.0._

Rules for picking up a task:
1. Read [00-overview](00-overview.md), [conventions](conventions.md), the module README and the design doc named in the task.
2. Size: **S** ≈ 1 day, **M** ≈ 2–4 days, **L** ≈ 1–2 weeks. Ask before starting an L: it needs a short design note first.
3. Do not start a task whose **Depends** are open. Project-level decisions are settled in [decisions.md](decisions.md); if a task seems to contradict one, stop and ask.
4. Finish with the [definition of done](08-testing-strategy.md#definition-of-done-any-task). Code `TODO(ID)` comments must match IDs here
   (`scripts/check_todo_ids.sh` verifies).

Status legend: ⬜ open · 🟡 partly done · ✅ done

## Good first tasks (no dependencies, low risk)
`CORE-3` · `STORE-0` · `MATCH-1` · `MATCH-3` · `FLOW-1` · `LOG-2` · `CI-3` · `CI-4` · `JOBS-1`

## Milestones (how the blocks come together)
| Milestone | Tasks | Demonstrates |
|---|---|---|
| **M0 — scaffold** ✅ | (this repo) | Log + Matcher + Credits + Node converge under loss in the simulator |
| **M1 — trustworthy sim** | CORE-3, STORE-0, ~~NET-0~~ ✅, ~~SIM-1~~ ✅, ~~SIM-2~~ ✅, ~~NODE-1~~ ✅, LOG-1 | richer faults, conformance suites, metrics, fork evidence |
| **M2 — real crypto & wire** | ~~CORE-1~~ ✅, ~~CORE-2~~ ✅, ~~ENC-1~~ ✅, ~~ENC-2~~ ✅, HELLO-1 🟡 | signed records that survive a byte round-trip |
| **M3 — two machines** | ~~ZMQ-0~~ ✅, ~~ZMQ-1~~ ✅, ~~DISC-1~~ ✅, ~~SEC-1~~ ✅, ~~SEC-1b~~ ✅, ~~STORE-1~~ ✅, ~~CLI-1~~ ✅ | `mosaic pub` on one host, `mosaic tail` on another, durable, over CURVE |
| **M4 — resilience** | ~~ACK-1~~ ✅, ~~HEADS-1~~ ✅, ~~FLOW-2~~ ✅, ~~FLOW-3~~ ✅, ~~FLOW-4~~ ✅, DISC-2 | writer can go offline; slow followers handled; LAN auto-discovery still open |
| **M5 — jobs** | ~~JOBS-1~~ ✅, ~~JOBS-2~~ ✅, ~~JOBS-3~~ ✅, JOBS-4 🟡 | call a service by name over the same network; capacity-aware ranking still open |
| **M6 — reach** | BIND-1, BIND-2, STORE-2, STORE-3, SEC-2, SEC-3 | C ABI + Python, retention, hardening |

## core
| ID | Task | Size | Depends | Acceptance criteria |
|---|---|---|---|---|
| CORE-1 | `Hasher` backed by libsodium (BLAKE2b or SHA-256); keep the reference SHA-256 as fallback | S | – | Same test vectors; selectable at build time; no libsodium types in public headers | ✅
| CORE-2 | Ed25519 `Signer`/`Verifier` (libsodium); `WriterId` = hex pubkey; keystore (generate/load/save, file mode 0600) | M | CORE-1 | `test_log.cpp` passes with real keys; forged/tampered cases still rejected; `InsecureTestSigner` only used in tests | ✅
| CORE-3 | `Record::operator==`, `from_hex(Hash)`, doctest `toString` for Record/Hash/LogKey | S | – | Failing tests print readable diffs; round-trip test for hex | ✅

## store
| ID | Task | Size | Depends | Acceptance criteria |
|---|---|---|---|---|
| STORE-0 | Turn `MemoryLogStore contract` into a reusable conformance suite (template/macro that takes a factory) | S | – | `MemoryLogStore` runs through it; adding a store = one line; covers NotNext, read bounds, keys(), multiple logs | ✅
| STORE-1 | SQLite `LogStore` (WAL, one table keyed `(writer, channel, seq)`) | M | STORE-0 | Passes conformance; survives reopen; 100k appends benchmark recorded in README | ✅
| STORE-2 | Append-only segment-file store with index | L | STORE-0, design note | Passes conformance; crash-safe (torn tail write recovers); documented format |
| STORE-3 | Retention + signed checkpoints | M | STORE-1, decision in [01](01-data-model.md) | Truncated replica can still verify new records from checkpoint; fork detection unaffected |

## log
| ID | Task | Size | Depends | Acceptance criteria |
|---|---|---|---|---|
| LOG-1 | Keep fork evidence (both records) and expose `forks()`; `Node::on_fork` receives both | M | – | Test: equivocating writer yields evidence retrievable after restart (with persistent store) | ✅
| LOG-2 | Batch ingest `ingest(span<const Record>)` with one store call | S | – | Same results as per-record ingest (property test); fewer store round trips | 🟡 store-level append_batch landed (MEASURE); log-layer ingest batching still open

## match
| ID | Task | Size | Depends | Acceptance criteria |
|---|---|---|---|---|
| MATCH-1 | Benchmark (1k/10k/100k subscriptions) and write the numbers in [03](03-matching-and-channels.md) | S | – | Reproducible command; compares with naive scan; claim "µs/match" confirmed or corrected | ✅
| MATCH-2 | `patterns()` listing for Hello | S | – | Returns active patterns; test with removals | ✅
| MATCH-3 | Erase empty literal entries on `remove` | S | – | Memory returns to baseline after add/remove churn (test via size accessor) | ✅
| MATCH-4 | Reduce matcher per-position allocations: reuse a scratch bitset instead of copying `open`, and exit word-wise early. MATCH-1 measured 6.7/22.7/191.7 µs at 1k/10k/100k | M | MATCH-1 | The MATCH-1 benchmark improves at 100k without changing semantics, and the randomised-vs-naive test still passes | ⬜ |

## flow
| ID | Task | Size | Depends | Acceptance criteria |
|---|---|---|---|---|
| FLOW-1 | Property tests for `CreditWindow`/`CreditGranter` (no overflow, clamping, never negative) | S | – | Randomized sequences; invariant `available <= capacity` | ✅
| FLOW-2 | Per-request credit accounting in Node (replace `reset()` on timeout) | M | – | Sim: delayed (not lost) responses do not inflate credit; window never exceeds capacity | ✅
| FLOW-3 | Handle `CreditMsg`; credit-gated streaming path | L | ENC-1 recommended | A slow consumer is dropped to FETCH mode and keeps up; producers never block (sim scenario) | ✅
| FLOW-4 | Slow-follower rule: if behind by > N, ignore live `RecordMsg` and catch up by FETCH only | M | – | Sim: follower slower than writer converges without unbounded memory | ✅

## proto
| ID | Task | Size | Depends | Acceptance criteria |
|---|---|---|---|---|
| ENC-1 | Binary encode/decode of all messages and Records | L | design note | Round-trip + fuzz tests; strict bounds; golden test vectors checked in; `std::optional<Message>` decoder | ✅
| ENC-2 | Unknown message id/version handling | S | ENC-1 | Unknown ids ignored, counted; version mismatch reported | ✅
| HELLO-1 | Handle `HelloMsg` (version + patterns), keep per-peer follow set | S | MATCH-2 | Peers learn who follows what; used by jobs worker discovery | 🟡 version + codec done; pattern advertisement moved to JOBS-2
| ACK-1 | Replica sends `AckMsg`; writer tracks acked seq and `min_replicas`; unacked retention | L | design note | Writer keeps records until `min_replicas` ACKs (fixes Dafka's missing setting); sim with replica loss | ✅
| HEADS-1 | `GetHeadsMsg`/`DirectHeadMsg` | M | HELLO-1 | Newcomer learns heads while the writer is offline and fetches from other replicas | ✅

## net
| ID | Task | Size | Depends | Acceptance criteria |
|---|---|---|---|---|
| NET-0 | Transport conformance suite | S | – | Checks all rules in `transport.h` contract; `SimTransport` passes | ✅
| SIM-1 | Latency, reordering, partitions, per-link loss in `SimNetwork` | M | – | Seeded; heal-then-converge test with a partition | ✅
| SIM-2 | Scenario runner: seed sweep (N seeds), prints failing seed, CLI `Mosaic sim` | M | SIM-1 | One command runs 1000 seeds of the convergence scenario | ✅
| ZMQ-0 | Split CMake into `Mosaic` (core, no ZeroMQ) and `MosaicZmq` (transport); find system libzmq first, fall back to building from source; rename `MOSAIC_BUILD_ZMQ_STACK` to say what it does | M | – | Core still builds and tests with no ZeroMQ installed; `MosaicZmq` links libzmq; docs explain both paths | ✅
| ZMQ-1 | ZeroMQ transport, layout B (PUB/SUB live + ROUTER/DEALER unicast), per [05](05-node-and-transport.md) | L | ZMQ-0, ENC-1, NET-0 | Passes NET-0 over loopback TCP; two processes converge; prefix-subscription rule from [03](03-matching-and-channels.md) tested | ✅
| DISC-1 | Wire `Discovery` into the ZeroMQ transport (static list) | M | ZMQ-1 | Nodes connect to peers from config | ✅
| DISC-2 | LAN beacon discovery | M | DISC-1 | Two hosts on a LAN find each other with no config | ✅
| DISC-3 | Gossip peer lists over the protocol | L | DISC-1, HELLO-1 | Removing any single node does not stop discovery | ⬜ blocked: zgossip's event stream is private to czmq (see upstream-notes); needs gossip over our own protocol

## node
| ID | Task | Size | Depends | Acceptance criteria |
|---|---|---|---|---|
| NODE-1 | Metrics counters (ingest results, fetches sent, credits exhausted, drops) | S | – | Accessor struct; asserted in sim tests | ✅
| NODE-2 | `unfollow`, publish batching | M | MATCH-3 | Unfollow stops delivery and unsubscribes at transport | ✅
| NODE-3 | Store-node preset (follow `>`, no app callback) | S | STORE-1 | One-liner to run a pure replica | ✅

## security
| ID | Task | Size | Depends | Acceptance criteria |
|---|---|---|---|---|
| SEC-1 | CURVE + ZAP allow-list on the ZeroMQ transport | M | ZMQ-1, CORE-2 | Unlisted key cannot connect (test) | ✅
| SEC-1b | ZAP handler for CURVE peer keys, so `PeerPins` is enforced at the handshake (open = pin on first use, closed = allow-list) | M | SEC-1 | An unlisted key cannot complete a handshake; a key that changed under a known identity is refused and counted; `curve_unauthenticated` stays 0 | ✅
| SEC-2 | Bad-peer scoring and rate limits | M | NODE-1 | Peer sending N bad records is ignored for T seconds (sim) | 🟡 limits and budgets done (BOUND); scoring/bans still open |
| SEC-3 | Writer key rotation (link record) | M | CORE-2, design note | Old log points to new log; verifier follows the link |

## jobs
| ID | Task | Size | Depends | Acceptance criteria |
|---|---|---|---|---|
| JOBS-1 | Encode `JobRequest`/`JobResult` into record payloads | S | – | Round-trip tests; documented in [06](06-jobs.md) | ✅
| JOBS-2 | Worker runtime: follow `svc.<name>`, run if rank 0, publish result | M | JOBS-1 | Sim: 3 workers, each job run by exactly one when all healthy | ✅
| JOBS-3 | Requester: submit, timeout → next rank, dedupe results by `job_id` | M | JOBS-2 | Sim: kill rank 0, job still completes once from the requester's view | ✅
| JOBS-4 | Capacity-aware ranking | M | FLOW-3, HELLO-1 | Overloaded worker is skipped | 🟡 blocked on hello pattern advertisement (HELLO-1 second half)

## tools, infra, docs, bindings
| ID | Task | Size | Depends | Acceptance criteria |
|---|---|---|---|---|
| CLI-1 | `Mosaic pub` / `Mosaic tail` | M | ZMQ-1 | Publish on host A, tail on host B | ✅
| CLI-4 | `mosaicd` daemon: ZPL config via czmq (zconfig), defaults printer, validation, run loop, status and SIGUSR1 | M | CLI-1 | Config documented and validated whole; a subscriber daemon replicates from a publishing CLI over CURVE and resumes after restart | ✅ |
| CLI-2 | `Mosaic verify <store>` verifies every chain in a store | S | STORE-1 | Detects tampered record | ✅
| CLI-3 | `Mosaic keygen` | S | CORE-2 | Writes keypair with safe permissions | ✅
| CI-1 | GitHub Actions: GCC + Clang, Linux, ZMQ stack OFF and ON | M | – | Green on main; badge in README | ✅
| CI-2 | Weekly "ZeroMQ ecosystem compiles" job: libzmq/czmq/malamute/dafka from pinned refs and master | M | CI-1 | Report artifact; failures link to [upstream-notes](../upstream-notes.md) | ✅
| CI-3 | ASan/UBSan (and TSan once threaded) build of the tests | S | – | Sanitizer job green | ✅
| CI-4 | `clang-format` pass over the whole tree + a format check in CI | S | – | `clang-format --dry-run --Werror` clean on every tracked source. The config said `UseTab: Always` while `conventions.md` and all code use 4 spaces, so the gate never passed; the config is fixed (now `UseTab: Never`) and 7 files still carry drift: `core/record.h`, `core/types.h`, `proto/messages.h`, `log/log.cpp`, `match/matcher.cpp`, `store/memory_log_store.cpp`, `test/malamute.cpp` | ✅
| DOC-1 | Remove remaining template leftovers (Doxygen `documentation/` pages and config) | S | – | Docs build describes the replicated-log library; root README already refreshed | ✅
| DOC-2 | Reference documentation builds: m.css + doxygen from the public headers and the design set, warnings are errors, and the build is part of the check | M | DOC-1 | `./scripts/docs.sh` produces a site; an undocumented public symbol or a broken reference fails it; `./scripts/ci.sh` runs it | ✅ |
| DOC-3 | Merge the docs generator into `docs/site/`, publish the site to GitHub Pages, link the prior art and add the guarantees/design pages | M | DOC-2 | One folder for documentation and its generator; the site deploys from `scripts/docs.sh` output; no bare upstream mention | ✅ |
| STYLE-1 | One written C++ style enforced by `.clang-format` (LLVM base, 100 cols) and `.clang-tidy` (correctness gated, naming enforced), with targeted `std::` imports in `.cpp` files | M | – | Format gated in CI; correctness lint clean; headers keep `std::` | ✅ |
| STYLE-2 | Work through the clang-tidy style backlog: braces on single-statement bodies (the largest family), `nodiscard`, designated initialisers, `contains`, const-array indexing, implicit bool conversions. `MOSAIC_LINT_STYLE=1 ./scripts/ci.sh` prints the count | M | STYLE-1 | The style count reaches zero and the lint step gates style as well as correctness | ⬜ |
| VERSION-1 | Versioning: `0.<closed milestones>.0` with CMake as the source of truth, an `inline constexpr` generated header, a drift test, a CHANGELOG and a version line in each design doc | M | – | `Mosaic version` reports 0.6.0; bumping CMake alone fails the test; the docs state which version they describe | ✅ |
| BIND-1 | Thin stable C ABI (`mosaic_c.h`) over the C++ core, for bindings only | L | M3 | ABI test in C; no C++ types exposed |
| BIND-2 | Python binding over the C ABI | M | BIND-1 | `pip install` demo publishes and tails |

## Decisions
All of D1–D6 are settled; see [decisions.md](decisions.md). One default remains open: matching is on **channel name only**
(no field/header matching) until a use-case needs more.
