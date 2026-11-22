# Proposal

## Why

The project is described in its own first line as "a brokerless, verifiable, replicated-log library **and daemon**", and the
direction document lists `mosaicd` as part of how this ships. There is no daemon.

The practical cost of that gap grew as the library matured. `NodeConfig` now carries twelve knobs - fetch batch and window,
timeouts, `max_serve`, `min_replicas`, `retention_limit`, `slow_follower_threshold`, control interval, record and pattern
limits, request budgets - and **an operator can set none of them**. The CLI accepts identity, store, listen address, port and
peer list, and nothing else. So the bounds and floors added by the last changes are configurable only by writing C++.

Everything needed exists as a library call. What is missing is the thing that turns it into a service: a config file an operator
can read and diff, validation that fails before the first byte moves, a long-running process that shuts down cleanly, and a way
to see what it is doing.

## What Changes

- `mosaicd <config>`: a daemon that opens identity, store and transport from a config file, follows its patterns, and runs the
  pump/tick loop until told to stop.
- Configuration in **ZPL** (ZeroMQ RFC 4), read with czmq's `zconfig` - the format zproject, malamute and zyre all use - with
  sections for node, limits, peers and follows. Using the ecosystem's format *and its parser* means a file Mosaic reads is a file
  the surrounding tooling already understands, and there is no second implementation of the RFC to keep compatible.
- czmq becomes a dependency of the CLI and daemon targets only. `core`..`node` remain dependency-free, and the configurations that
  prove that are untouched.
- Validation that reports **all** errors at once and exits before opening a socket, including unknown keys - a typo must not be
  a silent default.
- `mosaicd --defaults` prints a fully commented config generated from the same defaults the code uses, so documentation cannot
  drift from behaviour.
- `mosaicd --check <config>` validates and exits, for use in deployment scripts and tests.
- Compatibility is proven, not asserted: where czmq is available (the ecosystem build), the suite parses a file with czmq's
  reference `zconfig` and compares it with our own parse of the same file.
- Graceful shutdown on `SIGINT`/`SIGTERM`: finish the current step, close the store, exit 0.
- Periodic status on the configured log level, plus an on-demand dump (`SIGUSR1`) of the counters that now exist - ingest
  outcomes, repair activity, retention shortfall, throttles, handshake rejections.
- `--config` on `pub`, `tail` and `verify`, with explicit flags overriding the file, so the commands and the daemon share one
  source of truth.
- **Not** in scope: clustering, packaging, systemd units, a Prometheus endpoint, hot reload of config, or running more than one
  node per process. `SIGHUP` is deliberately not a reload: a restart is the supported way to change configuration, and saying so
  is better than a half-reload.

## Capabilities

### New Capabilities

- `daemon/mosaicd`: how the daemon is configured, what it validates before starting, how it shuts down, and what it reports
  while running.

### Modified Capabilities

- `cli/operations`: the existing commands gain `--config`, with command-line flags taking precedence over file values.

## Impact

- `standalone/source/` gains the daemon entry point and a config module; the existing `Mosaic` CLI keeps its commands and gains
  one flag each.
- `include/mosaic/` unchanged: the daemon consumes the library's public API only, which is the test of whether that API is
  sufficient for an operator-facing tool.
- `standalone/CMakeLists.txt` and the root build: czmq found on the system, else fetched from source with the existing pinned
  block; `Mosaic` and `MosaicTests` still build without it.
- Tests: config parsing and validation in the library-independent module, plus a two-process test where `mosaicd` replicates from
  a publishing CLI and its store grows.
- `README.md` and `docs/design/00-overview.md`: the daemon stops being a promise.
- `docs/design/TODO.md`: a new board row for `mosaicd` closes; discovery items (DISC-2/3) remain open and are what a daemon
  deployment will next want.
