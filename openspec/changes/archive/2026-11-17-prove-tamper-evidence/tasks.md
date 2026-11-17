# Tasks

## 1. Fork evidence

- [x] 1.1 Keep both records of a fork in `ReplicaLog`, keyed by log and sequence, with a configurable cap and a counter for
      overflow. Verified by: a test that feeds two validly signed records for one sequence and retrieves both.
- [x] 1.2 Expose the evidence through `Node` and deliver the offending record to `on_fork`. Verified by: a scenario asserting
      the callback payload and the sequence.
- [x] 1.3 Make evidence survive a restart where the store does. Verified by: a test that records a fork, closes the store,
      reopens and still reads the evidence.
- [x] 1.4 Confirm the accepted chain is untouched by evidence. Verified by: head hash before and after a fork is identical.

## 2. `Mosaic verify`

- [x] 2.1 Implement `Mosaic verify <store>`: walk logs, re-derive hashes, check signatures and links, report the first failure
      with log and sequence. Verified by: an intact store verifies and exits 0.
- [x] 2.2 Catch tampering and gaps. Verified by: a test altering a payload and one deleting a record, both exiting non-zero with
      the location named.
- [x] 2.3 Report a summary count of logs and records verified on success. Verified by: the command's output asserted in a test.

## 3. CI

- [x] 3.1 Build-and-test matrix: sodium on/off x ZeroMQ on/off. Verified by: `./scripts/ci.sh` runs all four and reports
      84 / 77 / 91 / 81 cases, all green. Clang is not installed on this machine, so the matrix is GCC-only today - the
      workflow installs `clang-format` and uses the default toolchain, and adding an explicit clang job is a one-line change
      once a clang toolchain is available here to verify it against.
- [x] 3.2 Sanitizer job (ASan + UBSan, TSan once anything is threaded). Verified by: `scripts/ci.sh` builds and runs the suite
      under ASan+UBSan green. Nothing is threaded, so TSan is deferred with that note in the script.
- [x] 3.3 Weekly "ZeroMQ ecosystem compiles" job from pinned refs. Verified by: the job's exact command
      (`cmake -S all -B build && cmake --build build -j && ./build/all/test/MosaicTests`) run here green (20801 assertions). The
      schedule itself cannot be exercised on this machine; findings go to `docs/upstream-notes.md` per D4.

## 4. Close out

- [x] 4.1 Remove the remaining template leftovers in `documentation/`. Verified by: the docs build describes the log library.
- [x] 4.2 Close LOG-1, CLI-2, CI-1, CI-2, CI-3, DOC-1 on the board. Verified by: `./scripts/check_todo_ids.sh` and a board
      greps clean.
- [x] 4.3 Definition of done: four build combinations, clang-format clean, spec rows updated. Verified by: the matrix and the
      format check.
