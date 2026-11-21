# Tasks

## 1. The message

- [x] 1.1 `PeersMsg` with a permanent message id, bounded entries (id, endpoints, age, hops), total decoder, golden vector.
      Verified by: round-trip, truncation, oversized list and an out-of-range age all produce the right status.
- [x] 1.2 Validate endpoints on decode (a `tcp://` endpoint that parses, an id that is a plausible node id). Verified by:
      malformed endpoint tests.

## 2. Gossip

- [x] 2.1 `Node` sends `PeersMsg` every `gossip_interval_ms` (jittered) to `gossip_fanout` random neighbours, carrying its freshest
      entries with ages. Verified by: a sim run where a four-node line converges.
- [x] 2.2 Receive and merge: refresh ages, prefer nearer hops for the same identity, cap hops, expire entries, bound the table with
      LRU eviction. Verified by: unit tests per rule and a sim test where an entry expires.
- [x] 2.3 Keep configured peers separate so gossip cannot change their endpoints. Verified by: a configured peer's address is
      unchanged after a conflicting entry arrives.
- [x] 2.4 Removing an introducer does not lose the peer it introduced. Verified by: the three-node sim in the spec.

## 3. Policy and abuse

- [x] 3.1 Gossiped candidates go through the normal dial path, so `PeerPins` decides before a link is used. Verified by: an
      unlisted key learned through gossip cannot complete a handshake.
- [x] 3.2 Oversized lists and malformed endpoints earn strikes through peer scoring; the table stays bounded under a flood.
      Verified by: a sim peer gossiping 1000 entries, asserting table bound and score.
- [x] 3.3 Budget the gossip traffic per peer so a burst cannot outrun the request budgets. Verified by: counters asserted under a
      storm test.

## 4. Close out

- [x] 4.1 Update `docs/design/05-node-and-transport.md` (gossip) and `docs/upstream-notes.md` (why zgossip is still not used), and
      remove the DISC-3 open-work note from `net/README.md`. Verified by: the docs describe the message and the policy.
- [x] 4.2 `mosaicd` configuration for gossip interval, fanout, hops, entry TTL and table bound, with validation and defaults.
      Verified by: config test and a two-daemon run that discovers a third through the second.
- [x] 4.3 Close DISC-3 on the board; `./scripts/check_todo_ids.sh`; `./scripts/ci.sh` green in all four configurations, site
      rebuilt.
