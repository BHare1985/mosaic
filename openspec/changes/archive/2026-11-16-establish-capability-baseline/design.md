# Design

## Context

See proposal.md - Why. What shapes the approach:

- The module dependency order is fixed and already enforced by review: `core` ← `store` ← `log` ← `proto` ← `net` ← `node`
  ← `jobs`, with `match` and `flow` as leaves. `docs/design/00-overview.md` states it; the CMake targets follow it.
- `docs/design/01`-`08` already describe contracts in prose, and each public header carries a contract comment. The specs
  here are extracted from those two sources, not invented.
- Everything currently implemented runs in one process against `SimNetwork`. No bytes leave a process, storage is a
  `std::map`, and the signer is `InsecureTestSigner`.
- Non-negotiables from `docs/design/conventions.md`: no exceptions in `core`..`node`, no clock reads, no globals, no locks,
  standard library only, warnings-as-errors.

## Goals / Non-Goals

**Goals:**

- One written behaviour contract per capability boundary, so every later change is a delta instead of a rewrite.
- Requirements phrased so the existing test suite is their evidence, and so a future test can be traced back to exactly one
  requirement.
- An explicit negative space: durability, real signatures, wire bytes, discovery and multi-process operation are declared
  *not* provided, so the next changes add requirements visibly.

**Non-Goals:**

- No behaviour change: no product header or source is touched. One test is added, for the verification order that no
  existing case pinned; everything else is documented where it already behaves as specified.
- No attempt to spec `jobs` end to end (only rendezvous ranking exists) or the job/security layers.
- No replacement of `docs/design/`: the docs keep the rationale and the "why"; the specs become the normative statement.
- No transport-specific requirements. ZeroMQ mapping stays out so it cannot leak into the contract.

## Decisions

**D1 - Capability boundaries follow the module dependency order, not the milestones.**
A capability is a behaviour that can own requirements for years; `core/record-integrity`, `store/log-store`,
`log/replicated-log`, `match/channel-patterns`, `flow/credit-control`, `net/transport`, `node/node-runtime` map one-to-one
onto modules that may only depend "leftward". *Alternative:* one capability per milestone (M1/M2/M3) - rejected because
milestones are transient and a milestone spec becomes dead prose the moment it ships.

**D2 - Storage and verification are separated by requirement, not just by class.**
`store/log-store` explicitly requires that a store never verifies, and `log/replicated-log` owns the verification taxonomy.
This is what makes a *passive* replica and an interchangeable backend possible, and it is the requirement that keeps SQLite
(LMDB, segment files) from growing a verification path of its own.

**D3 - The verification decision is specified as an ordering, not a set of checks.**
The same malformed record can be a gap *and* a forgery; without a fixed order two implementations disagree. The spec fixes
the order (signature → stored-history comparison → gap → link) and makes the outcome a single value, so the conformance test
is a total function over inputs.

**D4 - Matching is specified behaviourally, with the efficiency requirement separated.**
"Which channels match" is stated segment-wise and must agree with a naive definition; "cost does not scale with
non-matching subscribers" is a separate requirement. Splitting them means an implementation can be swapped (bitset, trie,
automaton) without touching the semantics, and the benchmark claim is testable on its own.

**D5 - Credit is specified as an invariant, not as a protocol.**
`available` never negative, never above capacity, one record consumes one unit, no send at zero. What carries credit
(`FetchMsg.count` today, an explicit grant message later) stays out of the requirement, so FLOW-3 can add a path without
rewriting the capability.

**D6 - Convergence is stated as the system's headline requirement, in `node/node-runtime`.**
Loss and duplication are named, and "no unverified record is stored or delivered" is stated as a separate requirement from
convergence so it cannot be traded away for liveness. This is the requirement every later change must keep green.

**D7 - Identity-to-log ownership is a baseline requirement even though signatures are weak.**
The writer identity is content-derived and stable across restarts. Specifying it now means `core/record-integrity`'s
cryptographic delta is a swap of the signing seam, not a change of log identity, so existing logs keep verifying.

## Risks / Trade-offs

- **Specs and docs drift apart** → The definition-of-done in `docs/design/08-testing-strategy.md` gains "the behaviour you
  changed has a spec delta" as the counterpart to its existing "update the design doc row"; where they disagree the spec
  wins and the doc is corrected in the same change.
- **Requirements that no test exercises** (e.g. bounded per-peer memory) → each requirement names its evidence in
  tasks.md; a requirement without evidence is marked as a gap task rather than left silently unverified.
- **Over-specifying the weak baseline** (asserting behaviour that will be replaced by CORE-2/ENC-1) → the negative-space
  requirements ("integrity before authenticity", "signature is outside hashed bytes") are written to survive that swap
  unchanged.
- **Capability granularity too fine** (seven specs for a 2k-line library) → accepted deliberately: the boundaries are the
  module boundaries, and they are the ones later changes must touch. Merging them later is a rename; un-merging is not.
- **`node/node-runtime` becomes a catch-all** → it is bounded to roles, repair state machine, clock injection, callbacks
  and convergence. Anything about *what a record is* or *what a transport does* is rejected in review.

## Migration Plan

No runtime migration. The repository gains `openspec/specs/` content through the normal archive path (`openspec sync specs`
or `openspec archive`), and the TODO board keeps owning work items. Task IDs are not copied into specs in either direction.

## Open Questions

- Whether `store/log-store` should later own retention and signed checkpoints (STORE-3) or whether checkpointing belongs in
  `log/replicated-log`. Deferrable: retention adds requirements either way and does not change these boundaries.
- Whether `jobs` deserves its own capability or belongs inside `node/node-runtime` once the worker runtime exists. Deferrable
  until JOBS-1/2 land.
