# Tasks

## 1. Configuration

- [x] 1.1 Make czmq available to the CLI and daemon targets: `find_package` first, falling back to the pinned source fetch
      already used for the ecosystem check. Verified by: the daemon builds both ways, and `Mosaic` plus `MosaicTests` still build
      with no czmq at all (the `core` configuration).
- [x] 1.2 Read ZPL with `zconfig_load` and expand `${NAME}` with `zconfig_resolve`, failing and naming the variable when one is
      unset. Verified by: unit tests for nesting, a repeated section, a comment, a missing value, an unset variable, and a
      property that steps out of its section.
- [x] 1.3 Add the defaults table and have the parser fill absent settings from it. Verified by: a file naming only identity,
      store and a peer yields a complete config equal to the documented defaults elsewhere.
- [x] 1.4 Validate everything at once: unknown sections or properties, unparsable numbers, out-of-range values and missing
      required settings are collected and reported together. Verified by: a file with three faults lists all three and exits
      non-zero.
- [x] 1.5 `mosaicd --defaults` prints a commented ZPL config from that table. Verified by: feeding the output back (with
      identity, store and peers filled in) starts the daemon and reports the same values.
- [x] 1.6 `mosaicd --check <config>` validates and exits without opening a socket. Verified by: a good file exits 0 and a bad
      one exits non-zero with its errors.

## 2. The daemon

- [x] 2.1 Build store, transport, node and follows from a validated config. Verified by: a test that starts the daemon against a
      temp directory and asserts the store is created and the follows are registered.
- [ ] 2.2 The run loop: pump, tick, then wait for transport activity with a bounded timeout; time read once per iteration.
      **Partially done:** the loop reads time once per iteration and paces itself with a 5ms bounded sleep, and a quiet daemon
      does not spin. The task asks for a *socket* wait, which `Transport` does not expose - adding one is a contract change
      beyond this change. Left open rather than quietly redefined.
      Verified by: a quiet daemon does not spin (CPU-checked in a test by counting iterations over a fixed simulated period).
- [ ] 2.3 Graceful shutdown on `SIGINT`/`SIGTERM`: finish the iteration, close the store, exit 0. Verified by: interrupting a
      running daemon leaves every accepted record present after restart.
- [ ] 2.4 Periodic status line plus an on-demand counter dump (`SIGUSR1`) with the counters the node already exposes. Verified
      by: the line names accepted, duplicates, gaps, forks, repair requests, retention shortfall, throttles and handshake
      rejections.

## 3. One file for both tools

- [x] 3.1 `--config` on `pub`, `tail` and `verify`, ignoring daemon-only keys and letting explicit flags win. Verified by: one
      file drives a `tail` run, and a store flag overrides the file's store.

## 4. Prove it

- [ ] 4.1 Two-process test: a `mosaicd` replicating from a publishing CLI, its store growing, then a restart resuming without
      loss. Verified by: the test asserting record counts on both sides and after the restart.
- [ ] 4.2 Runbook in the daemon's docs: how to tell "receiving nothing" from "rejecting handshakes" from "being throttled" from
      "repairing repeatedly", each mapped to a counter. Verified by: the section names each counter and what it implies.

## 5. Close out

- [ ] 5.0 Record the dependency policy the config decision implies: czmq is allowed in the CLI and daemon targets, never in
      `core`..`node`, and it must not change what the `core` and `no-crypto` configurations build. Verified by: `00-overview.md`
      and `conventions.md` state it, and the two dependency-free configurations still build and pass.

- [ ] 5.1 Update `README.md` and `docs/design/00-overview.md` so the daemon is described as shipped, with the command and the
      file format. Verified by: the README's build-and-run section includes the daemon.
- [ ] 5.2 Add and close a board row for `mosaicd`. Verified by: `./scripts/check_todo_ids.sh` exits 0 and the row reads done.
- [ ] 5.3 Definition of done: `./scripts/ci.sh` green in all four configurations plus sanitizers, clang-format clean, and the
      new capability's spec rows updated. Verified by: the CI run.
