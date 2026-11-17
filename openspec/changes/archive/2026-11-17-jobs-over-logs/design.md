# Design

## Context

See proposal.md. What shapes it: the node already provides publish/follow/on_record over signed durable logs; `rendezvous_rank`
is implemented and tested; the wire codec (`core/encoding.h`) exists; and everything is single-threaded and clock-injected, so
timeouts must be driven by the caller's clock rather than slept on.

## Goals / Non-Goals

**Goals:** call a service by name and get one result back, with no coordinator and no new transport; survive a worker dying;
let a requester restart mid-job.

**Non-Goals:** exactly-once (it is not achievable here and the spec says so), cancellation, priorities, streaming, worker
discovery from hello patterns (JOBS-4).

## Decisions

**D1 - Envelopes are a small binary format, not JSON.**
`job_id`, reply channel, timeout, body for a request; `job_id`, ok flag, body for a result, all length-prefixed via
`ByteWriter`. *Alternative:* JSON - rejected: it would add a dependency and a parser to `jobs`, for a structure with five
fields.

**D2 - Ordering of runs comes from the log, waiting comes from the requester's timeout.**
Every worker sees the request at the same position in the same log, so the only thing that differs is how long each waits:
rank r waits `r x timeout_ms / workers`. That gives a dead rank-0 a bounded delay before rank-1 acts, without any coordination.
*Alternative:* requester re-publishes to direct the retry - rejected: it turns one request into traffic proportional to failures
and makes the requester a coordinator.

**D3 - Workers check the reply channel before running.**
A worker follows its own `res.<requester>` channel, so it can see whether a result already exists before its turn. That is what
keeps the common case to one run even with generous timeouts, and it is cheap because the reply channel is already replicated.

**D4 - "Run" is a callback, and its side effects are the application's problem.**
The runtime guarantees at-least-once *invocation* and nothing about the handler. Idempotency by `job_id` is the documented
requirement, not something the library can enforce.

**D5 - The worker set is static in this change.**
A worker takes the list of known workers. Deriving it from `HelloMsg.patterns` is JOBS-4, which needs its own decision about
what a peer advertises and how it is trusted. *Trade-off:* a deployment must list its workers - the same integration cost
discovery already has (DISC-1).

## Risks / Trade-offs

- **Duplicate runs on a slow rank 0** -> documented, absorbed by first-result-per-job-id, and the handler contract says
  idempotent by job id.
- **A large worker set makes later ranks wait long** -> the wait is proportional to rank share; a real deployment with many
  workers will want capacity-aware ranking (JOBS-4) rather than longer timeouts.
- **Reply channel namespace** -> `res.<requester>` is a channel, so two requesters with the same id collide. The requester id is
  the node id, which is already unique per identity.
- **Clock injection** -> every timeout is computed from `now_ms` passed in, so scenarios are deterministic and a test can jump an
  hour in a loop.

## Open Questions

- Whether results should carry the worker's identity explicitly or be inferred from the record's writer. Deferrable: the record
  already carries a signature, so the writer identity is authoritative.
- Whether to expire old result records. Deferrable to retention (STORE-3).
