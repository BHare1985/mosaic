# jobs — calling services over logs

**Purpose:** a job system built on top of logs and channels, with no central broker. Today only
`rendezvous_rank()` exists (pick workers deterministically). `job.h` holds the planned envelopes.
Design: [06-jobs](../../../docs/design/06-jobs.md).

**Rules**
- Jobs may depend on everything below them; nothing below may depend on jobs. Logs don't know jobs exist.
- Delivery is at-least-once; handlers must be idempotent by `job_id`.

**Open work:** JOBS-1..JOBS-4.
