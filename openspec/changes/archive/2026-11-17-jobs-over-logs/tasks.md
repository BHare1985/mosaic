# Tasks

## 1. Envelopes

- [x] 1.1 Encode and decode `JobRequest` and `JobResult` with the shared byte writer/reader. Verified by: round-trip tests over
      every field, and a truncated envelope decoding to nothing rather than a half-filled struct.
- [x] 1.2 Give the envelopes their content types so a record says what it carries. Verified by: a test asserting the request and
      result records carry the documented content types.

## 2. Worker runtime

- [x] 2.1 Rank workers for a job id and schedule the run by rank: rank r acts after r x the request timeout. Verified by: a sim
      scenario where three healthy workers produce exactly one run.
- [x] 2.2 Follow the reply channel named by a request and skip the run when a result for that job id already exists. Verified by:
      a scenario where a finished job reaches a later-ranked worker and does not run again.
- [x] 2.3 Publish the result as an ordinary record on the reply channel. Verified by: the requester reads it back and the record
      carries the worker's signature.

## 3. Requester

- [x] 3.1 Submit a request record and accept only the first result per job id. Verified by: two results for one job id leave the
      requester with the first, once.
- [x] 3.2 Report a timeout naming the job id and the worker set it expected. Verified by: a scenario with no workers asserting
      the timeout, not a silent success.
- [x] 3.3 Survive a restart: re-read the request from the requester's own log and accept a late result. Verified by: a scenario
      where the requester is reconstructed from its store before the result arrives.

## 4. Prove it and close the board

- [x] 4.1 Sim scenario: rank 0 never answers, rank 1 runs the job after its wait. Verified by: the scenario asserting one run
      and one result.
- [x] 4.2 Close JOBS-1..JOBS-3 on the board and record JOBS-4 as blocked on hello pattern advertisement. Verified by:
      `./scripts/check_todo_ids.sh` and no stale `TODO(JOBS-1..3)` in code.
- [x] 4.3 Definition of done: four build combinations green, clang-format clean, module README and design doc row updated.
      Verified by: the matrix and the format check.
