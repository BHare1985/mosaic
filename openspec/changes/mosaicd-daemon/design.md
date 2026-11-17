# Design

## Context

See proposal.md. What shapes this: the library is single-threaded with injected time and a caller-owned loop; `NodeConfig` has
twelve settings and no file path; the CLI is a thin driver (design D7 of the transport change) with six flags; there is no
config parsing anywhere in the tree, and `core`..`node` may not take third-party dependencies. `scripts/ci.sh` is the gate, and
`mosaicd` will be built by the same CMake targets.

## Goals / Non-Goals

**Goals:** one file that fully describes a node; failure before the first byte moves; a process that stops cleanly and says what
it is doing; the CLI and the daemon reading the same file.

**Non-Goals:** hot reload, multiple nodes per process, privilege dropping, systemd integration, metrics exposition protocols.

## Decisions

**D1 - A flat `key = value` file, parsed by us, with dotted names.**
`node.id = a`, `limits.max_record_bytes = 262144`, `peer.b = 127.0.0.1:6200,<curve>`. Repetition builds a list. `#` starts a
comment. No nesting, no quoting rules, no parser dependency.
*Alternatives:* TOML/YAML (a library dependency in the tool for a twelve-field file, and YAML's failure modes are its own
project); JSON (no comments, which an operator-facing file needs). *Trade-off:* no nesting means longer keys - acceptable for a
flat configuration, and it keeps the parser under a page.

**D2 - Unknown keys are errors, and all errors are reported together.**
A typo in `min_replicas` must not be a silent default; that is how someone ends up running without the floor they configured.
Validation collects every problem before exiting, because fixing one-at-a-time against a long restart loop is punishment, not
safety.

**D3 - Defaults live in one place: a table used by the parser and by `--defaults`.**
The table is the single source of truth: it fills absent settings and renders the commented file. *Alternative:* documenting the
defaults in a Markdown table - rejected: it drifts, and the drift is invisible until someone deploys with a stale value.

**D4 - The loop is the CLI's loop, with a poll timeout instead of a sleep.**
`pump`, `tick`, then wait on the transport with a bounded timeout so a quiet node does not spin and a busy one does not add
latency. Time comes from a monotonic clock read once per iteration, which is the only clock the process owns - the library still
takes `now_ms` as a parameter.

**D5 - Signals set a flag; the loop decides when to stop.**
A handler only sets a `volatile sig_atomic_t`. The loop checks it at the top of an iteration, finishes that iteration, closes the
store, and returns. Nothing in the library is touched from a signal handler.

**D6 - Observability is the counters plus a periodic line.**
One structured line per interval at info level (`accepted`, `duplicates`, `gaps`, `forks`, `fetch_sent`, `deficit`,
`throttled`, `handshake_rejects`), and a full dump on demand. *Alternative:* a Prometheus endpoint - deferred: it is a protocol,
a binder and a parser for a capability nobody has asked for yet, and the counters are the substance.

**D7 - The CLI reads the same file but ignores daemon-only keys.**
So one file can describe a deployment and the operator can still run `Mosaic tail --config mosaicd.conf events.>`. Explicit
flags win, so scripts stay predictable. Unknown keys are still errors - a typo is a typo in both tools.

## Risks / Trade-offs

- **A hand-written parser is a place to introduce bugs** -> it is a page of code with tests for every failure mode, and the
  alternative is a dependency the project has avoided everywhere else.
- **A daemon invites scope creep (reload, metrics, plugins)** -> the non-goals list what is deliberately absent, and reload is
  explicitly a restart.
- **Two configuration paths (file and flags) can diverge** -> the precedence rule is stated in the spec and tested, and both read
  the same table.
- **Logging from a single-threaded loop can stall the loop** -> the periodic line is written after the iteration's work and is
  bounded in size.

## Open Questions

- Whether the daemon should expose a local control socket (`mosaicd --status`, dump counters, request a snapshot) instead of
  relying on signals. Deferrable: signals are enough to observe; a control socket is a protocol to design.
- Whether authorization rules (the separate change) land in this file as `allow.<pattern> = <writer>`. Very likely yes, and the
  flat format already accommodates it; the decision belongs with that change.
