# Tasks

## 1. The link

- [x] 1.1 Define the `mosaic/rotation` payload (format, algorithm, successor key, start sequence) and its codec with round-trip
      and hostile-input tests. Verified by: a total decoder on truncated and oversized payloads.
- [x] 1.2 `Mosaic rotate <channel> --to <key>`: write the link with the current key, print the new log key, refuse a successor
      equal to the current key. Verified by: CLI test over a temp store.
- [x] 1.3 Prove the link is an ordinary record: a replica that ignores rotation still stores and replicates it. Verified by: an
      ingest-then-read test with no rotation support in the follower path.

## 2. Resolution

- [x] 2.1 `RotationChain::resolve(trusted_root, channel)` walking the store, bounded by depth and refusing cycles and repeats.
      Verified by: chain of 1, chain of N, chain longer than the limit, cycle, and a link signed by the wrong key.
- [x] 2.2 Cache the resolved successor set per (root, channel) and invalidate it on ingest of a new link. Verified by: a test
      that authorizes A, ingests a link to B, and observes the authorization change without a restart.
- [x] 2.3 Refuse resolution from an untrusted root: a link found in someone else's log grants nothing. Verified by: a stranger's
      claim is ignored.

## 3. Authorization

- [x] 3.1 Extend `ChannelRules` with rotation-root entries and consult the chain when deciding. Verified by: rotated key
      accepted, predecessor refused, unrelated key refused.
- [x] 3.2 `mosaicd` configuration accepts a rotation root in `allow_writer`, documented with an example. Verified by: config
      validation test.
- [x] 3.3 Fork detection and `Mosaic verify` still work across a rotation, and a fork in the successor's log is reported.
      Verified by: fork evidence test with a rotated successor.

## 4. Close out

- [x] 4.1 Record the decision in `docs/design/01-data-model.md` (the log key gains a successor relation) and
      `docs/design/07-security.md` (what rotation cannot protect against). Verified by: both docs name the limits.
- [x] 4.2 Close SEC-3 on the board; `./scripts/check_todo_ids.sh`; `./scripts/ci.sh` green in all four configurations.
