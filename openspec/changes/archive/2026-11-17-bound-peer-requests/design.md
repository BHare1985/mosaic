# Design

## Context

See proposal.md. The codec already bounds frames (`kMaxFrameBytes`, 1 MiB) and inner lengths; ZAP decides who may connect; the
repair path already has per-peer credit. What is missing is limits on the *application* surface: record size, pattern count,
request rate, queue depth.

## Goals / Non-Goals

**Goals:** a node's cost per peer is bounded and visible; nothing legitimate breaks; no new interfaces to learn.

**Non-Goals:** scoring or banning peers over time, admission control at the socket, per-tenant accounting, budget for *outbound*
traffic (that is credit's job).

## Decisions

**D1 - The record limit is enforced where the record enters, not where it is stored.**
`ReplicaLog::ingest` and `Node::publish` both check it, so a backend can never receive one. *Alternative:* store-level checks -
rejected: every backend would reimplement it, and the store is supposed to stay dumb.

**D2 - Limits are per peer and counted, never fatal.**
Exceeding a budget drops the request and increments a counter. A node MUST NOT disconnect or blacklist on a budget breach in this
change: one bursty but honest peer would look like a partition, and getting that wrong is worse than the abuse it prevents.

**D3 - Caps are conservative defaults, configurable per node.**
Patterns per request (default 64), requests per interval (default matching the control interval), pending jobs (default 1024),
record size (default 256 KiB, matching `kMaxRecordBytes`). A deployment that wants different numbers says so.

**D4 - The job layer's bound lives in the worker, not in the envelope.**
Bounding *pending* jobs is a runtime concern; the wire format stays as it is.

## Risks / Trade-offs

- **A too-small record limit is a compatibility break** -> it is configurable and the default matches the documented maximum, so
  anything that worked before still works.
- **Rate limits can delay a legitimate repair** -> the repair path has credit and retry; a throttled request is retried, not
  forgotten, and the test asserts convergence under throttling.
- **Counters multiply** -> they exist to make abuse visible; the metrics surface is already established (NODE-1).

## Open Questions

- Whether the record limit should be per channel. Deferrable: one limit is simpler and the failure is explicit.
