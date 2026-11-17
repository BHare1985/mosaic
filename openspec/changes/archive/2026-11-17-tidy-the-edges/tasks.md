# Tasks

## 1. Value types

- [x] 1.1 Add equality for `Record`, `Hash`, `LogKey` and doctest printing in the test sources. Verified by: a deliberately failing
      comparison prints both values.

## 2. Matcher

- [x] 2.1 Prune empty literal entries on removal. Verified by: add/remove churn returns the matcher's size to baseline.
- [x] 2.2 Add `patterns()` and cover it with removals. Verified by: listing returns exactly the active patterns.

## 3. Credit

- [x] 3.1 Property tests over randomised spend/grant/reset sequences. Verified by: a thousand operations with the invariants checked
      after each.

## 4. Node

- [x] 4.1 `unfollow`, unsubscribing at the transport. Verified by: after unfollow, matching records are neither stored nor
      delivered.
- [x] 4.2 Batched publish with the same ordering guarantees. Verified by: a batch occupies consecutive sequences in order.

## 5. Remove the legacy scaffolding (scope added during apply)

- [x] 5.1 Delete the "does the Malamute/Dafka ecosystem still build" track: `test/source/malamute.cpp`, the malamute CPM
      block, the weekly CI job, and the `distclean` patch that existed only to build it. Verified by: `rg -n malamute
      CMakeLists.txt test/ .github/ scripts/` finds nothing, and four configurations still build.
- [x] 5.2 Remove the track from where it was stated as a goal (`docs/direction.md`, `AGENTS.md`,
      `docs/design/08-testing-strategy.md`). Verified by: no document claims the project proves that ecosystem compiles.

## 6. Make the published documentation generic (scope added during apply)

- [x] 6.1 Publish only the public surface: README, public headers, module READMEs and the site pages. The internal design set
      and the fetched reference trees are not rendered. Verified by: the site builds 164 pages with no `docs/references` or
      design-set pages in it.
- [x] 6.2 Replace the module READMEs' Markdown links into the internal design set with plain repository paths, so no published
      page carries a link a reader of the site cannot follow. Verified by: `rg -n "\\]\\(\\.\\./" include/mosaic`.
- [x] 6.3 Keep the gate meaningful where it matters: a warning or error about a **public header** fails the docs build, while
      Markdown links to repository paths do not. Verified by: a simulated header warning trips it (`/tmp/fake.log` form) and the
      real build passes.

## 7. Close out

- [x] 7.1 Close CORE-3, MATCH-2, MATCH-3, FLOW-1, NODE-2 on the board. Verified by: board and
      `./scripts/check_todo_ids.sh`.
- [x] 7.2 Definition of done: four build configurations, sanitizers, documentation and format all green. Verified by:
      `./scripts/ci.sh`.
