# Proposal

## Why

Any writer with any key may publish to any channel. For an event bus that is a defensible default. For the job layer it is not:
`svc.<name>` is just a channel, so **any peer can publish a `JobRequest` and have a worker run it** - and the worker's handler is
arbitrary application code with arbitrary side effects. The security design has listed channel authorization as "layer 3, later"
since the first draft; the jobs layer is what makes it urgent.

There is a second, quieter case: an audit log shared across organisations is only meaningful if a given channel's history is
provably written by the party that owns it. Today any writer can add to any log channel it likes - each write is attributed and
verifiable, but nothing says it was allowed.

## What Changes

- Per-channel rules that name which writer identities may write, enforced at ingest on followers *and* at publish on the writer,
  so an unauthorised record is never stored or replicated as accepted history.
- Default remains open (`>` allow), so nothing existing breaks; a rule is opt-in per channel pattern.
- `svc.*` can be closed in one line, which is what makes the job layer safe to expose.
- Rules are configuration per node in this change.
- **Not** in scope: a replicated membership channel (the natural follow-up: publish the rules as signed records so they converge
  like everything else), revocation semantics, and per-peer read authorization.

## Capabilities

### New Capabilities

- `security/channel-authorization`: which writer identities may write a channel, and what a node does with a record that violates
  a rule.

## Impact

- `include/mosaic/node/node.h`, `source/node/node.cpp`: rule set, checked in `handle_record` before ingest and in `publish`.
- `source/log/log.cpp` unchanged: authorization is a node concern, not a log one - the log stays "verify what you are given".
- `standalone/source/main.cpp`: `--allow-writer <pattern>=<writer-id>[,<writer-id>...]`.
- Tests: sim scenarios for an allowed writer, a rejected writer, and a closed `svc.` channel.
- `docs/design/07-security.md`: layer 3 stops being a promise.
- `docs/design/TODO.md`: SEC-3 (key rotation) stays open and is referenced as the follow-up that makes revocation meaningful.
