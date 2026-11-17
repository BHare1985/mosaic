# Design

## Context

See proposal.md. Identity is already content-derived (hex public key), records are already verified before anything else, and the
job layer routes work by channel name. Configuration is per node (peers, keys, limits). The design set calls authorization
"layer 3, later"; this is that layer, scoped to writes.

## Goals / Non-Goals

**Goals:** stop unauthorised writes at the boundary; keep the open default; make the rule set small enough to reason about.

**Non-Goals:** replicated membership (follow-up), revocation and rotation (SEC-3), read authorization, per-record permission
objects.

## Decisions

**D1 - Rules are static configuration per node in this change.**
`pattern -> allowed writer ids`, matched with the existing `Matcher` so the rule language is the subscription language. It ships
without inventing a bootstrap problem: a membership channel needs to answer "who may write the membership channel", which is the
same question one level up. *Alternative considered:* publish rules as signed records on `members.<org>` - the right long-term
answer, and explicitly recorded as the follow-up once revocation exists.

**D2 - Enforcement happens before storage, in both directions.**
On a follower, a record that violates a rule is rejected before `ReplicaLog::ingest`, so a replica never stores history it would
have to hide. On a writer, `publish` checks the same rules so it cannot create a log its own peers will reject.

**D3 - Default open, and a closed channel must be named.**
A missing rule means "any verified writer". This keeps every existing deployment working and makes closure a deliberate act.

**D4 - Rejections are counted per pattern, not per writer.**
Per-writer counters would grow with the network; per-pattern counters answer the operator's actual question ("is my rule
firing?") without an unbounded map.

## Risks / Trade-offs

- **A mis-typed writer id silently blocks data** -> the rejection counter and the CLI's rule echo exist for exactly this; the
  manual two-host demo becomes a test case with a rule.
- **Static rules diverge between nodes** -> two nodes with different rules disagree about what is accepted, which is visible
  (one stores, one rejects). Replicated membership removes the divergence, which is why it is the follow-up.
- **Authorization is not authentication of content** -> a permitted writer can still lie; signatures prove who, rules prove
  allowance. Both are needed and neither replaces the other.

## Open Questions

- Whether rules should be able to name a *group* (a set of writers resolved from another channel). Deferrable: lists first.
- Whether `verify` should report records that are valid but unauthorised. Deferrable until membership replicates.
