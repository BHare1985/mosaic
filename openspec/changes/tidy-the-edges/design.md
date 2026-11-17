# Design

## Context

See proposal.md. These are independent small items; the only design content is where each belongs, and the constraint that this
change must not absorb anything needing a decision.

## Goals / Non-Goals

**Goals:** close the small items without inventing scope; keep each fix covered by a test that would have caught it.

**Non-Goals:** anything architectural. If an item turns out to need a decision, it leaves this change.

## Decisions

**D1 - Equality and printing on value types, defaulted.**
`operator==` is defaulted where the members are comparable; doctest `toString` lives in the test sources so the library does not
depend on the test framework.

**D2 - Matcher removal prunes, and listing is a new accessor.**
Pruning is straightforward; the listing accessor is what HELLO pattern advertisement will need, so it is written now with a test
rather than later under pressure.

**D3 - Property tests use a seeded generator, not a fuzzer.**
Deterministic and fast in the default suite, consistent with how simulation already works.

**D4 - `unfollow` unsubscribes at the transport too.**
Stopping delivery but leaving the subscription in place would keep paying for traffic the node drops.

## Risks / Trade-offs

- **Buckets drift into design decisions** -> the rule is written into the proposal; an item that needs a decision moves out.
- **Pruning could invalidate ids** -> the matcher's contract already allows id reuse; tests cover removal followed by reuse.

## Open Questions

None that need answering before starting.
