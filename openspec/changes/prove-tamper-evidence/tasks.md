# Tasks

## 1. Fork evidence

- [ ] 1.1 Keep both records of a fork in `ReplicaLog`, keyed by log and sequence, with a configurable cap and a counter for
      overflow. Verified by: a test that feeds two validly signed records for one sequence and retrieves both.
- [ ] 1.2 Expose the evidence through `Node` and deliver the offending record to `on_fork`. Verified by: a scenario asserting
      the callback payload and the sequence.
- [ ] 1.3 Make evidence survive a restart where the store does. Verified by: a test that records a fork, closes the store,
      reopens and still reads the evidence.
- [ ] 1.4 Confirm the accepted chain is untouched by evidence. Verified by: head hash before and after a fork is identical.

## 2. `Mosaic verify`

- [ ] 2.1 Implement `Mosaic verify <store>`: walk logs, re-derive hashes, check signatures and links, report the first failure
      with log and sequence. Verified by: an intact store verifies and exits 0.
- [ ] 2.2 Catch tampering and gaps. Verified by: a test altering a payload and one deleting a record, both exiting non-zero with
      the location named.
- [ ] 2.3 Report a summary count of logs and records verified on success. Verified by: the command's output asserted in a test.

## 3. CI

- [ ] 3.1 Build-and-test matrix: GCC and Clang, sodium on/off, ZeroMQ on/off. Verified by: a green run and a deliberately broken
      commit failing.
- [ ] 3.2 Sanitizer job (ASan + UBSan, TSan once anything is threaded). Verified by: the job green on the current tree.
- [ ] 3.3 Weekly "ZeroMQ ecosystem compiles" job from pinned refs, recording results where the project's own notes point.
      Verified by: a scheduled run producing a report.

## 4. Close out

- [ ] 4.1 Remove the remaining template leftovers in `documentation/`. Verified by: the docs build describes the log library.
- [ ] 4.2 Close LOG-1, CLI-2, CI-1, CI-2, CI-3, DOC-1 on the board. Verified by: `./scripts/check_todo_ids.sh` and a board
      greps clean.
- [ ] 4.3 Definition of done: four build combinations, clang-format clean, spec rows updated. Verified by: the matrix and the
      format check.
