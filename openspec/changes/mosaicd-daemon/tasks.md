# Tasks

## 1. Configuration

- [ ] 1.1 Write the ZPL parser (RFC 4): indentation-defined sections, `name = value` properties, `#` comments, a repeated
      section name building a list, and `${NAME}` expansion from the environment. Verified by: unit tests for nesting, a repeated
      section, a comment, a missing value, an unset variable, and a property that steps out of its section.
- [ ] 1.2 Cross-check the parser against the reference implementation: in the ecosystem build, load one fixture with czmq's
      `zconfig` and with ours and assert the same tree. Verified by: the comparison running where czmq is available, and reported
      as skipped (not silently absent) where it is not.
- [ ] 1.3 Add the defaults table and have the parser fill absent settings from it. Verified by: a file naming only identity,
      store and a peer yields a complete config equal to the documented defaults elsewhere.
- [ ] 1.4 Validate everything at once: unknown sections or properties, unparsable numbers, out-of-range values and missing
      required settings are collected and reported together. Verified by: a file with three faults lists all three and exits
      non-zero.
- [ ] 1.5 `mosaicd --defaults` prints a commented ZPL config from that table. Verified by: feeding the output back (with
      identity, store and peers filled in) starts the daemon and reports the same values.
- [ ] 1.6 `mosaicd --check <config>` validates and exits without opening a socket. Verified by: a good file exits 0 and a bad
      one exits non-zero with its errors.

## 2. The daemon

- [ ] 2.1 Build store, transport, node and follows from a validated config. Verified by: a test that starts the daemon against a
      temp directory and asserts the store is created and the follows are registered.
- [ ] 2.2 The run loop: pump, tick, then wait for transport activity with a bounded timeout; time read once per iteration.
      Verified by: a quiet daemon does not spin (CPU-checked in a test by counting iterations over a fixed simulated period).
- [ ] 2.3 Graceful shutdown on `SIGINT`/`SIGTERM`: finish the iteration, close the store, exit 0. Verified by: interrupting a
      running daemon leaves every accepted record present after restart.
- [ ] 2.4 Periodic status line plus an on-demand counter dump (`SIGUSR1`) with the counters the node already exposes. Verified
      by: the line names accepted, duplicates, gaps, forks, repair requests, retention shortfall, throttles and handshake
      rejections.

## 3. One file for both tools

- [ ] 3.1 `--config` on `pub`, `tail` and `verify`, ignoring daemon-only keys and letting explicit flags win. Verified by: one
      file drives a `tail` run, and a store flag overrides the file's store.

## 4. Prove it

- [ ] 4.1 Two-process test: a `mosaicd` replicating from a publishing CLI, its store growing, then a restart resuming without
      loss. Verified by: the test asserting record counts on both sides and after the restart.
- [ ] 4.2 Runbook in the daemon's docs: how to tell "receiving nothing" from "rejecting handshakes" from "being throttled" from
      "repairing repeatedly", each mapped to a counter. Verified by: the section names each counter and what it implies.

## 5. Close out

- [ ] 5.1 Update `README.md` and `docs/design/00-overview.md` so the daemon is described as shipped, with the command and the
      file format. Verified by: the README's build-and-run section includes the daemon.
- [ ] 5.2 Add and close a board row for `mosaicd`. Verified by: `./scripts/check_todo_ids.sh` exits 0 and the row reads done.
- [ ] 5.3 Definition of done: `./scripts/ci.sh` green in all four configurations plus sanitizers, clang-format clean, and the
      new capability's spec rows updated. Verified by: the CI run.
