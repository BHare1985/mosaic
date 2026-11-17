# Proposal

## Why

The project's one-line pitch is *verifiable*, and two of the three things that make that true are missing from the code path:

1. **Equivocation is detected but not provable.** `ReplicaLog::ingest` returns `Fork` and `Node::on_fork` fires - and then the
   conflicting record is discarded. A writer that showed two peers different histories leaves no evidence behind, so "we can
   prove a writer equivocated" is currently false.
2. **Nothing verifies a store.** `Mosaic tail` proves a live path works; there is no command that walks the logs on disk and
   re-checks every chain and signature. A tamper-evident log without a verify command is missing its punchline.
3. **None of it runs on a schedule.** Every green in this project so far was produced by hand on one machine. That is not the
   same claim as "it builds and passes".

This change closes all three, because they are the same claim made three ways: the evidence exists, you can check it yourself,
and it is checked whenever anything changes.

## What Changes

- A replica keeps both records of a fork, keyed by log and sequence, and exposes them (`forks()`), surviving a restart when the
  store does.
- `Node::on_fork` delivers the competing record so an application can act on the evidence immediately.
- `Mosaic verify <store>` walks every log in a store, re-verifies each signature and hash link, and reports the first break with
  the log and sequence that failed.
- CI: build and test matrix (GCC + Clang, ZeroMQ stack on and off, libsodium on and off), sanitizer job, and the existing
  "ZeroMQ still builds in 2026" check as a scheduled job.
- **Not** in scope: gossiping fork evidence to other nodes, or deciding what a fork means for membership. Keeping and showing the
  evidence is the prerequisite for both.

## Capabilities

### Modified Capabilities

- `log/replicated-log`: a detected fork is retained as evidence, not discarded, and is readable after a restart.
- `cli/operations`: a new `verify` command that re-checks a store end to end.

## Impact

- `include/mosaic/log/log.h`, `source/log/log.cpp`: fork evidence storage in the log layer (the store stays dumb).
- `include/mosaic/node/node.h`, `source/node/node.cpp`: expose evidence and deliver both records to the callback.
- `standalone/source/main.cpp`: `verify` subcommand.
- `.github`/CI config (CI-1, CI-2, CI-3), `documentation/` cleanup (DOC-1).
- `docs/design/TODO.md`: LOG-1, CLI-2, CI-1, CI-2, CI-3, DOC-1 close.
