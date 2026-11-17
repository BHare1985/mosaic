# 06 — Jobs / services over logs (✅ ranking, envelopes, worker and requester; JOBS-4 open)

[Malamute](https://github.com/zeromq/malamute)'s "service" pattern without a broker. A job is just records on channels, so it inherits verification,
replication, replay and flow control.

```
requester                                   workers (follow svc.resize)
 publish Record on  svc.resize  ───────────►  every worker sees the request (same log, same order)
   body = JobRequest{job_id, reply_channel, timeout}
 follow res.<requester>
                                              rank = rendezvous_rank(job_id, known_workers)
                                              rank[0] runs it; others wait `timeout_ms`
 ◄───────────── publish JobResult on res.<requester>   (job_id, ok, body)
 if no result by timeout_ms: worker rank[1] runs it, then rank[2], ...
```
## Rules
- **Deterministic assignment** with no coordination: `rendezvous_rank` (✅). Removing a worker only reassigns its own jobs.
- **At-least-once.** A slow rank[0] and a rank[1] may both run the job. Handlers must be idempotent by `job_id`; the
  requester accepts the first result per `job_id` and ignores later ones.
- **Worker set** = peers that follow `svc.<service>` (from `HelloMsg.patterns`, HELLO-1). Until then, a static list, which is
  what `JobWorker` takes. Deriving it is JOBS-4 and needs its own decision about what a peer advertises.
- **Capacity:** workers advertise credit; ranking skips workers with none (JOBS-4, depends on FLOW-3).
- **Results are records** on the requester's own channel, so the requester can replay them after a restart.
- **Not provided:** exactly-once, cancellation, priorities, streaming results. Consider later, one at a time.

## Mapping to Malamute
`SERVICE OFFER` = follow `svc.<name>`; `SERVICE SEND` = publish on `svc.<name>`; broker's round-robin = rendezvous rank;
`CONFIRM`/re-queue (never implemented in Malamute) = timeout to next rank.
