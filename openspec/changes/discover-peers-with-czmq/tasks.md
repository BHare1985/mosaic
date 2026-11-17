# Tasks

## 1. The port

- [ ] 1.1 Define `PeerDiscovery`: a source of candidate peers that reports through the daemon's loop, with the static list as its
      first implementation (today's behaviour, expressed through the port). Verified by: the static configuration path behaves
      exactly as before, with the existing two-process test still passing.
- [ ] 1.2 Marshalling: what an actor publishes crosses into the node loop over an inproc socket drained by the same poll, with no
      shared state. Verified by: an in-process test driving a stub source through the socket and observing peers appear.
- [ ] 1.3 Candidate bookkeeping: track last-seen, bound the set, expire silent peers, and dial each peer once. Verified by: churn
      tests asserting the set returns to the number of peers present and a silent peer is dropped.

## 2. czmq actors

- [ ] 2.1 `zbeacon`: announce and listen on a scoped service name, feeding candidates through the port. Verified by: a manual
      two-host check documented in the runbook; the announcement string and scoping asserted in an in-process test.
- [ ] 2.2 `zgossip`: exchange peer lists with discovered nodes, identities and endpoints only. Verified by: an in-process test
      with two gossip endpoints exchanging a known peer, and a review check that no key material crosses.
- [ ] 2.3 Daemon configuration: a `discover` section selecting static, beacon, gossip or several, with the cluster name echoed at
      startup. Verified by: `mosaicd --check` accepting and rejecting the documented shapes.

## 3. Trust and integration

- [ ] 3.1 Prove discovery grants nothing: a discovered peer in closed key mode is refused by the policy, and its refusal is
      counted rather than retried forever. Verified by: a scenario with a discovered-but-unlisted peer.
- [ ] 3.2 Prove negotiation still applies to discovered peers. Verified by: a discovered peer announcing an unsupported version
      has its payload refused.
- [ ] 3.3 Record the thread exemption in `docs/design/00-overview.md` and `conventions.md`: actors own their threads, everything
      crosses as a message, and `core`..`node` remain single-threaded. Verified by: both files state it once and agree.

## 4. Close out

- [ ] 4.1 Close DISC-2 and DISC-3 on the board, keeping the static list as the documented default. Verified by: board and
      `./scripts/check_todo_ids.sh`.
- [ ] 4.2 Runbook: what discovery does and does not reveal, and how to tell "no peer is announcing" from "a discovered peer was
      refused" in the counters. Verified by: the section names both counters.
- [ ] 4.3 Definition of done: `./scripts/ci.sh` green including the documentation step, clang-format clean, spec rows updated.
      Verified by: the CI run.
