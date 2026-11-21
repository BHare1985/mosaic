# Proposal

## Why

`adopt-cpp-style` put a written style in place and split the gate: formatting is enforced, correctness lint (`bugprone`,
`clang-analyzer`) is enforced, and the remaining style families are *reported* - 681 suggestions today, up from 521 when the gate
was created, because code kept arriving faster than the backlog shrank. A number that only grows stops being read, and the lint
step's value decays to "the part that is red is ignored".

The style rules themselves are not in question: each family that was adopted (`braces`, `nodiscard`, designated initialisers,
`contains`, `.at`, implicit bool conversions) was chosen deliberately, and the code that follows them is easier to review than the
code that does not. What is missing is finishing the job, and then closing the hole that let it fill up again.

## What Changes

- Every style family in the backlog is fixed, in one mechanical pass per family, in an order chosen so that later families do not
  re-touch earlier files.
- `./scripts/ci.sh` gates style as well as correctness: `MOSAIC_LINT_STYLE` disappears, and `clang-tidy` runs with the full check
  set on every configuration.
- The two rules that cannot be automated are written down and checked in review: no `std::` in headers, no `using namespace`
  anywhere (the micro-decisions `adopt-cpp-style` recorded in prose).
- A short "adding a check" note records how the next style rule gets adopted: turn it on, fix what it finds, gate it - never the
  other order.

## Capabilities

### New Capabilities

- `docs/cpp-style`: the enforced style, the gate, and how a new rule is adopted.

### Modified Capabilities

- None. `docs/reference-site` is about publishing documentation, not about how code is written.
