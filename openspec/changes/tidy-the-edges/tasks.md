# Tasks

## 1. Value types

- [ ] 1.1 Add equality for `Record`, `Hash`, `LogKey` and doctest printing in the test sources. Verified by: a deliberately failing
      comparison prints both values.

## 2. Matcher

- [ ] 2.1 Prune empty literal entries on removal. Verified by: add/remove churn returns the matcher's size to baseline.
- [ ] 2.2 Add `patterns()` and cover it with removals. Verified by: listing returns exactly the active patterns.

## 3. Credit

- [ ] 3.1 Property tests over randomised spend/grant/reset sequences. Verified by: a thousand operations with the invariants checked
      after each.

## 4. Node

- [ ] 4.1 `unfollow`, unsubscribing at the transport. Verified by: after unfollow, matching records are neither stored nor
      delivered.
- [ ] 4.2 Batched publish with the same ordering guarantees. Verified by: a batch occupies consecutive sequences in order.

## 5. Close out

- [ ] 5.1 Close CORE-3, MATCH-2, MATCH-3, FLOW-1, NODE-2 on the board. Verified by: board and `./scripts/check_todo_ids.sh`.
- [ ] 5.2 Definition of done: four build combinations, clang-format clean. Verified by: the matrix and format check.
