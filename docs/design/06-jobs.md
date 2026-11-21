# 06 — Jobs / services over logs (✅ ranking, envelopes, worker and requester, derived worker set)

_Describes 0.7.0._

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
- **Worker set** = the peers whose hellos advertise following `svc.<service>`, plus this node when it follows the service
  itself (HELLO-1, JOBS-4 - ✅). An explicit list still wins, which is what a single-node deployment and a test use.
- **Capacity is a penalty, not a filter.** Each worker advertises one byte of spare capacity derived from its own queue
  (`pending >= max_pending / 2` means none). The *slot* a worker takes comes from the plain rendezvous rank; a worker that
  advertises no room waits one extra cycle. Penalising rather than filtering is what keeps two workers whose views differ by a
  hello interval from both believing they are due first, and keeps a job landing somewhere when everyone is busy.
- **Advertisements are hints.** A peer that lies about its patterns or its capacity loses messages or work; it gains nothing,
  because delivery, acceptance and authorization never read them (`proto/peer-advertisement`).
- **Results are records** on the requester's own channel, so the requester can replay them after a restart.
- **Not provided:** exactly-once, cancellation, priorities, streaming results. Consider later, one at a time.

## Mapping to Malamute
`SERVICE OFFER` = follow `svc.<name>`; `SERVICE SEND` = publish on `svc.<name>`; broker's round-robin = rendezvous rank;
`CONFIRM`/re-queue (never implemented in Malamute) = timeout to next rank.
