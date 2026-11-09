# 02 — Protocol (🟡 values implemented, wire encoding ⬜)

Derived from Dafka's RECORD / HEAD / FETCH / ACK / HELLO set (see
[../malamute-and-dafka-system-design.md](../malamute-and-dafka-system-design.md)), with `FETCH.count` doubling as
the requester's credit.

## Messages (`proto/messages.h`)

| Message | From → to | Path | Meaning | Status |
|---|---|---|---|---|
| `RecordMsg` | writer → followers | publish (best effort) | live delivery of a new record | ✅ |
| `HeadMsg` | writer → followers | publish, every `head_interval_ms` | "my log ends at seq/hash" | ✅ |
| `FetchMsg` | follower → peer | unicast | send records `[from, from+count)` | ✅ |
| `DirectRecordMsg` | peer → follower | unicast | repair delivery; returns one credit each | ✅ |
| `AckMsg` | replica → writer | unicast | "stored up to seq" (ACK-1) | ⬜ |
| `GetHeadsMsg` / `DirectHeadMsg` | newcomer ↔ peers | unicast | learn heads while writer offline (HEADS-1) | ⬜ |
| `CreditMsg` | receiver → sender | unicast | explicit credit on streaming paths (FLOW-3) | ⬜ |
| `HelloMsg` | both | unicast on connect | version + followed patterns (HELLO-1) | ⬜ |

## Node behaviour (the state machines)

**Writer** (per log it publishes): `publish` → append, announce `RecordMsg`; `tick` → every `head_interval_ms` publish `HeadMsg`.

**Replica** (per followed log), driven by `handle_*` in `source/node/node.cpp`:
```
on RecordMsg/DirectRecordMsg(r):  (ignore if channel not followed)
    known_head[log] = max(known_head[log], r.seq)
    ingest(r):  Accepted → on_record(r); if direct and still behind → request_fetch
                Gap      → request_fetch(from = next_seq)
                Fork     → on_fork(r)
                others   → drop
on HeadMsg(h): known_head = max(...); if next_seq <= h.seq → request_fetch
request_fetch(log, peer):
    if nothing missing → return
    if a request for this range to this peer is in flight and not timed out → return        (dedupe)
    n = credits[peer].take_up_to(min(missing, fetch_batch)); if n == 0 → return              (flow control)
    send FetchMsg{from = next_seq, count = n}; remember {sent_ms, end = from+n-1}
tick: completed requests are forgotten; timed-out ones reset the peer's credits and retry.
```
**Responder:** `FetchMsg` → `store.read(key, from, min(count, max_serve))` → `DirectRecordMsg` for each. Any node can
respond for any log it holds, not only the writer. (This is what makes stores optional.)

## Delivery guarantees
- Within a log: in order, gap-free, at most once to the app (`on_record` fires once per seq).
- Liveness: a follower eventually converges if some reachable node holds the missing records and messages are
  *eventually* delivered (property tested under 30% loss).
- Nothing across logs. No exactly-once for jobs (see [06](06-jobs.md)).

## Versioning
`kProtocolVersion = 1` carried in `HelloMsg` (HELLO-1). Unknown message ids are ignored, not errors (ENC-2). A breaking
change bumps the version and ships behind negotiation. Do not renumber ids.

## Wire encoding (ENC-1, not started)
Requirements: length-prefixed, big-endian, bounded sizes (reject > `max_record_bytes`), one id byte per message, strict decoder
that returns `std::optional<Message>` and never reads out of bounds. Needs fuzz + round-trip tests. Reuse the framing helpers
in `source/core/record.cpp` (`put_u32` etc.) by moving them into a shared header. Record on the wire = `canonical_bytes` + `u32 sig_len` + `sig`.
