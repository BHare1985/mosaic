# jobs — calling services over logs

**Purpose:** a job system built on top of logs and channels, with no central broker. `rendezvous_rank()`
picks the runner deterministically, `job.h` defines the envelope and its encoding, and `runtime.h`
provides `JobWorker` (run when ranked) and `JobRequester` (wait for the first result).
Design: 06-jobs (`docs/design/06-jobs.md`), contract: `openspec/specs/jobs/service-calls`.

**Rules**
- Jobs may depend on everything below them; nothing below may depend on jobs. Logs don't know jobs exist.
- Delivery is at-least-once; handlers must be idempotent by `job_id`.
- A node runs either a `JobWorker` or a `JobRequester`, not both: both own the node's single record
  callback. Composing them is application work, and the constraint is deliberate - it keeps `Node`
  from growing a subscription registry for a layer that sits above it.
- The worker set is derived from the peers whose hellos advertise following `svc.<service>`, plus this
  node when it follows it. The explicit-list constructor still wins, for a single node or a test
  (JOBS-4).

**Open work:** none.
