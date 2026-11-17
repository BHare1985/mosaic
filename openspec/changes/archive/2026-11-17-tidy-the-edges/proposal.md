# Proposal

## Why

The board's remaining small items are all the same kind of thing: a missing equality operator, a matcher that leaks empty entries,
credit arithmetic with no property test, a `keys()` that a loop was misusing. None needs a design decision, and none is worth its
own proposal - but together they are the difference between a codebase that is finished and one that is merely working.

This change is the bucket for that work, with one rule: nothing enters it that needs a decision. Anything that does gets its own
change.

## What Changes

- `Record`/`Hash`/`LogKey` gain equality and doctest printing, so a failing test shows the values instead of "CHECK( a == b )".
- The matcher erases empty literal entries on removal, and can list its patterns (needed by hello pattern advertisement later).
- Property tests for `CreditWindow`/`CreditGranter`: never negative, never above capacity, monotone under grants.
- `unfollow` and publish batching on the node.
- **Not** in scope: anything with a design question (discovery, retention, rotation, capacity ranking) - those are separate changes
  or explicitly deferred.

## Capabilities

### Modified Capabilities

- `match/channel-patterns`: subscription removal releases its storage and patterns are listable.
- `flow/credit-control`: the invariants are enforced by randomised property tests, not only by examples.
- `node/node-runtime`: following is reversible and publishing can batch.

## Impact

- `include/mosaic/core/record.h`, `core/types.h`, `match/matcher.h`, `flow/credits.h`, `node/node.h` and their sources.
- Tests: property tests and updated matcher tests.
- `docs/design/TODO.md`: CORE-3, MATCH-2, MATCH-3, FLOW-1, NODE-2 close.
