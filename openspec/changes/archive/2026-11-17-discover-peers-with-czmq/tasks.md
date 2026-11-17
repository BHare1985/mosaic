# Tasks

## 1. The port

- [x] 1.1 Define `PeerDiscovery`: a source of candidate peers that reports through the daemon's loop, with the static list as its
      first implementation (today's behaviour, expressed through the port). Verified by: the static configuration path behaves
      exactly as before, with the existing two-process test still passing.
- [x] 1.2 Marshalling: what an actor publishes crosses into the node loop over an inproc socket drained by the same poll, with no
      shared state. Verified by: an in-process test driving a stub source through the socket and observing peers appear.
- [x] 1.3 Candidate bookkeeping: track last-seen, bound the set, expire silent peers, and dial each peer once. Verified by: churn
      tests asserting the set returns to the number of peers present and a silent peer is dropped.

## 2. czmq actors

_Gossip is not in this change: czmq's `zgossip` emits its tuples as a private zproto frame, so it cannot be consumed from outside
czmq. The finding is in `docs/upstream-notes.md`, the decision is DISC-3 on the board, and the proposal records the scope change._

- [x] 2.1 `zbeacon`: announce and listen on a cluster-scoped beacon, feeding candidates through the port. Verified by: two
      `mosaicd` processes with no configured peers found each other on this host (`discovered=1` on both), an in-process pair test
      asserting the announcement contents, and an isolation test showing a different cluster is ignored. The cluster is the
      beacon's *filter* - sending it as a command asserts inside czmq and aborts the process, which is how that was learned.
- [x] 2.3 Daemon configuration: a `discover` section selecting static, beacon, gossip or several, with the cluster name echoed at
      startup. Verified by: `mosaicd --check` accepting and rejecting the documented shapes.

## 3. Trust and integration

- [x] 3.1 Prove discovery grants nothing: a discovered peer in closed key mode is refused by the policy, and its refusal is
      counted rather than retried forever. Verified by: a scenario with a discovered-but-unlisted peer.
- [x] 3.2 Prove negotiation still applies to discovered peers. Verified by: a discovered peer announcing an unsupported version
      has its payload refused.
- [x] 3.3 Record the thread exemption in `docs/design/00-overview.md` and `conventions.md`: actors own their threads, everything
      crosses as a message, and `core`..`node` remain single-threaded. Verified by: both files state it once and agree.

## 4. Close out

- [x] 4.1 Close DISC-2 and DISC-3 on the board, keeping the static list as the documented default. Verified by: board and
      `./scripts/check_todo_ids.sh`.
- [x] 4.2 Runbook: what discovery does and does not reveal, and how to tell "no peer is announcing" from "a discovered peer was
      refused" in the counters. Verified by: `09-operations.md` has a Discovery section naming `discovered=N`,
      `handshake_rejects`, the evasive/expired distinction, and the gossip limitation.
- [x] 4.3 Definition of done: `./scripts/ci.sh` green including the documentation step, clang-format clean, spec rows updated.
      Verified by: the CI run.
