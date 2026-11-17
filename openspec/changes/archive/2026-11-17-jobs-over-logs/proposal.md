# Proposal

## Why

The project's own goal names it: "an optional job layer lets you call services by name over the same logs". Everything under it
now works - signed durable logs, verified replication, repair, discovery by configuration, head exchange with the writer gone,
and credit-paced delivery - and `rendezvous_rank` has been sitting there since M0 with an envelope struct marked "design only".
This is the last piece of the original scope, and the one that makes the "agent bus" use case real.

What a job needs beyond a message bus is: exactly-one-run in practice without coordination, at-least-once delivery that survives
a worker dying mid-job, and a result the requester can read back after a restart.

## What Changes

- A binary encoding for `JobRequest` and `JobResult`, carried in ordinary record payloads, so a job inherits signing, ordering,
  durability, replication and replay for free.
- A worker runtime: follow `svc.<service>`, rank the known workers for the job id, run when ranked first, publish the result.
- Ranked retry: a worker that is not first waits, then runs if no result appeared - so a dead worker delays a job instead of
  losing it.
- A requester: publish the request, follow `res.<requester>`, accept the first result per `job_id`, and report the timeout when
  nothing arrives.
- **BREAKING** for `jobs/job.h`: it stops being "design only" and becomes the wire contract.
- **Not** in scope: exactly-once, cancellation, priorities, streaming results, dynamic worker discovery via hello patterns
  (JOBS-4 stays open: it needs HELLO pattern advertisement, which is its own decision).

## Capabilities

### New Capabilities

- `jobs/service-calls`: the job envelope encoding, ranked execution, at-least-once semantics and the requester contract.

## Impact

- `include/mosaic/jobs/job.h` (envelopes + codec), `include/mosaic/jobs/worker.h`, `include/mosaic/jobs/requester.h`, sources.
- `include/mosaic/node/node.h` unchanged in principle: jobs consume the node's public API only.
- Tests: envelope round trips, and sim scenarios for "each job runs once", "rank 0 dies, rank 1 runs it", "requester accepts one
  result per job_id".
- `docs/design/TODO.md`: JOBS-1..JOBS-3 close; JOBS-4 recorded as blocked on HELLO patterns.
