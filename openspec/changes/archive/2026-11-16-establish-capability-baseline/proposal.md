# Proposal

## Why

Mosaic's behaviour is currently specified twice, in prose and in code, and neither is authoritative: `docs/design/01`–`08`
describe contracts, `// TODO(ID)` comments cite `docs/design/TODO.md`, and `openspec/specs/` is empty. Nothing records *what
the system must do* independently of the implementation that happens to exist. As a result, an agent or contributor can
change behaviour in `core`..`node` without touching any formal requirement, and the next milestone (real transport, real
crypto) has no baseline to regress against.

This change establishes that baseline: it converts the contracts that are **already implemented and green** into durable
capability specs, so every later change becomes a delta against a written requirement rather than a rewrite of prose.

## What Changes

- Introduce seven capability specs covering the behaviours that exist today (in-memory, single-process, simulated network).
- Each requirement states an invariant that the current tests already exercise, plus the contract from the module header.
- Explicitly record what the baseline does **not** promise (durability, real signatures, wire bytes, discovery), so later
  changes add requirements instead of quietly widening behaviour.
- No behaviour changes: every requirement is satisfied by the current implementation. One test is added where the baseline
  had no evidence at all (the fixed order of the verification checks), so the suite ends at 23 cases / 5298 assertions and is
  the evidence for the rest.

## Capabilities

### New Capabilities

- `core/record-integrity`: canonical record bytes, hash chain, the hash/sign/verify seams, and the frozen on-disk hash
  formula that every other capability depends on.
- `store/log-store`: the append-only per-log persistence contract (`append`, `read`, `head`, `keys`) and its ordering and
  bounds guarantees.
- `log/replicated-log`: writer sequence assignment and signing, replica verification order, and the
  `Accepted/Duplicate/Gap/Fork/BadSignature/BadChain` outcome taxonomy.
- `match/channel-patterns`: channel naming, the pattern language (`*`, `>`), and the matching semantics that define
  "who receives what".
- `flow/credit-control`: credit windows, the drop-don't-block principle, and where credits apply (repair today).
- `net/transport`: the `Transport` contract — best-effort delivery, no self-delivery, publish filtered by subscription,
  silent drop for unknown peers.
- `node/node-runtime`: the node's three roles, the gap-repair state machine, head announcement, and the app callbacks.

### Modified Capabilities

None. `openspec/specs/` is empty; these are first-time baselines.

## Impact

- `openspec/specs/` gains seven capability specs; nothing else in the repo changes.
- `docs/design/01`–`05` keep their role as rationale and must now be read as *explanation of* these specs rather than the
  source of truth for behaviour. Where the two disagree, the spec wins and the doc gets corrected.
- Later changes (`verifiable-records-and-wire-format`, `two-machines-transport-and-durability`) modify these specs by delta,
  which is why the baseline states the negative space (no durability, no real crypto) explicitly.
- `docs/design/TODO.md` keeps owning *work*; these specs own *behaviour*. Task IDs are not duplicated into specs.
