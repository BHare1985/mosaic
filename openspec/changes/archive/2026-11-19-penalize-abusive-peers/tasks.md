# Tasks

## 1. Scoring

- [x] 1.1 Add `PeerScore` to the per-peer table with strikes, last strike, ban count and ban deadline; add fault weights as named
      constants in one place. Verified by: unit test on the weight table and on decay arithmetic.
- [x] 1.2 Award strikes where each fault is already counted: undecodable message, failed signature or chain, oversized record,
      unrequested record, unsupported hello version. Verified by: one test per kind, each asserting the counter and the strike.
- [x] 1.3 A `SimNetwork` scenario in which a misbehaving peer's messages are ignored while a healthy peer's are not.
      Verified by: the healthy peer's records still arrive.
- [x] 1.4 Strike decay over time, proportional to the period, with no history per peer. Verified by: a peer that faults once per
      window never reaches the threshold; one that faults in a burst does.

## 2. Bans

- [x] 2.1 Start a ban at the threshold with a doubling duration and a ceiling; expire it on its own; lift it early on a
      successful hello plus a verifiable record. Verified by: duration doubling test and an expiry test that needs no operator
      action.
- [x] 2.2 Drop a banned peer's messages before decoding, at the single inbound choke point; do not serve it repair; exclude it
      from job ranking. Verified by: banned fetch gets no reply; a banned follower in a sim gets nothing while a healthy one
      converges.
- [x] 2.3 Prove the boundary: a banned peer's correctly signed record still lands if it arrives from another link.
      Verified by: a sim test that bans a peer, then delivers the same record via a second peer.

## 3. Visibility

- [x] 3.1 Counters: `bans_started`, `peer_bans` (currently banned peers), `strikes_by_kind`. Verified by: asserted in the
      relevant sim tests.
- [x] 3.2 `mosaicd` reports bans on `SIGUSR1` and logs one line per ban edge (start, expiry). Verified by: `mosaicd` output in a
      scripted run.
- [x] 3.3 Document the weights, thresholds and durations in `docs/design/07-security.md`, with the "bans are not belief" rule.
      Verified by: the doc names the numbers and the boundary.

## 4. Close out

- [x] 4.1 Close SEC-2 on the board; `./scripts/check_todo_ids.sh`; `./scripts/ci.sh` green in all four configurations.
