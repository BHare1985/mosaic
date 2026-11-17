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

**D1 - The config format is ZPL (ZeroMQ RFC 4), parsed in-tree.**
The same format czmq's `zconfig` reads, and that zproject, malamute and zyre use: indentation defines sections, `name = value` sets
a property, `#` comments, and a repeated section name builds a list. A Mosaic config is therefore legible to the ecosystem's
tooling, and to anyone who has ever configured a ZeroMQ project.

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

*Alternatives:* czmq's `zconfig` class as a mandatory dependency - rejected: czmq is fetched only in the ecosystem build, so the
daemon would stop building in the configurations CI runs, for a parser of about 150 lines. TOML/YAML - rejected: a dependency, and
a format no other ZeroMQ tool speaks. JSON - rejected: no comments, and an operator file needs them.
*Trade-off:* we own the parser, so we own its bugs. That is why the suite cross-checks against czmq's `zconfig` wherever czmq is
present (D8), turning "ZPL-compatible" into something tested rather than claimed.

**D1a - `${NAME}` is expanded from the environment, and an unset variable is an error.**
Paths and CURVE keys are exactly what operators keep out of files in a repository. czmq offers the same through
`zconfig_resolve`; we expand on load and refuse to start on an unset variable rather than substituting an empty string into a key.

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

**D8 - ZPL compatibility is tested against the reference implementation where it is available.**
The ecosystem build already fetches and links czmq into the tests. In that configuration the suite loads one fixture with czmq's
`zconfig` and with our parser and asserts the same tree, so a divergence from the real ZPL implementation fails CI instead of
surfacing in a deployment. In configurations without czmq the fixture is checked against expected values only, and the comparison
is reported as skipped rather than silently absent.

**D7 - The CLI reads the same file but ignores daemon-only keys.**
So one file can describe a deployment and the operator can still run `Mosaic tail --config mosaicd.conf events.>`. Explicit
flags win, so scripts stay predictable. Unknown keys are still errors - a typo is a typo in both tools.

## Risks / Trade-offs

- **A hand-written ZPL parser is a place to introduce bugs** -> tests for every failure mode, plus the cross-check against
  czmq's `zconfig` in the ecosystem build, which is the reference implementation of the same RFC.
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
