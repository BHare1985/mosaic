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

**D1 - Configuration is ZPL, read with czmq's `zconfig`.**
The format czmq's `zconfig` reads, and that zproject, malamute and zyre use: indentation defines sections, `name = value` sets a
property, `#` comments, and a repeated section name builds a list. A Mosaic config is therefore legible to the ecosystem's
tooling, and to anyone who has ever configured a ZeroMQ project - and we do not write a second parser for a format that already
has a maintained one.

```zpl
node
    id = a
    store = /var/lib/mosaic/a.db
    keys = /etc/mosaic/a.key
    listen = 127.0.0.1:6200
limits
    max_record_bytes = 262144
    min_replicas = 2
peer
    id = b
    endpoint = 127.0.0.1:6100
    curve = ${B_CURVE_KEY}
follow
    events.>
```

*Alternatives:* parsing ZPL ourselves (the first draft of this change) - rejected on reflection: it is ~150 lines we would own and
have to keep compatible, next to a class that already implements the RFC, and the CLI and daemon already require libzmq so the
dependency is free where it is needed. TOML/YAML - rejected: a dependency *and* a format no other ZeroMQ tool speaks. JSON -
rejected: no comments, and an operator file needs them.
*Consequence:* czmq becomes a dependency of the **CLI and daemon targets only**. `core`..`node` stay dependency-free and the
`core` / `no-crypto` build configurations stay exactly as they are; the daemon is meaningless without libzmq, so this adds nothing
to what a deployer already installs. The dependency is resolved system-first (`find_package`) and only falls back to the existing
fetch-from-source path.

**D1a - `${NAME}` is expanded from the environment, and an unset variable is an error.**
Paths and CURVE keys are exactly what operators keep out of files in a repository. `zconfig_resolve` does the expansion and
returns the name of a variable it could not resolve, which is what lets us fail naming it instead of substituting an empty string
into a key.

**D2 - Unknown sections and properties are errors, and all errors are reported together.**
A typo in `limits.min_replicas` must not be a silent default; that is how someone ends up running without the floor they
configured.
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

**D8 - The parser is czmq's, so compatibility is not a claim we have to test.**
With D1 settled, "ZPL-compatible" needs no cross-check fixture: the reference implementation *is* the implementation. What still
needs testing is our own layer on top - which sections and properties we accept, that unknown ones are errors, that defaults fill
what is absent, and that `--defaults` round-trips through the same table.

**D7 - The CLI reads the same file but ignores daemon-only keys.**
So one file can describe a deployment and the operator can still run `Mosaic tail --config mosaicd.conf events.>`. Explicit
flags win, so scripts stay predictable. Unknown keys are still errors - a typo is a typo in both tools.

## Risks / Trade-offs

- **czmq is not a healthy dependency in 2026** (our own ecosystem check had to patch its CMake to build) -> accepted knowingly: the
  parts used here are the thread-free, least volatile ones (`zconfig`, `zcert`), and the alternative is maintaining a parser
  ourselves. Where czmq would import an actor thread - `zauth`, `zbeacon`, `zgossip` - the choice is made case by case: our ZAP
  handler stays ours, discovery is a separate proposal that names the threads it introduces.
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
