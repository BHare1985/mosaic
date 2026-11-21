# 02 — Protocol (🟡 values implemented, wire encoding ⬜)

_Describes 0.6.0._

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
| `HelloMsg` | both | unicast, retried until answered | version now; followed patterns with JOBS-2 | 🟡 |

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

## The hello: what a peer learns about a peer (HELLO-1, ✅)

`HelloMsg` is sent on every link on the head interval, for the life of the link, and carries three things:

| Field | Meaning | What a receiver may do with it |
|---|---|---|
| `version` | the protocol version (now **2**) | refuse payloads from a version it cannot speak, and count it |
| `patterns` | what the sender follows, deduplicated, bounded by `max_head_patterns` | choose whom to ask for a service; **never** decide delivery, acceptance or authorization |
| `spare_capacity` | 0..255 units of spare work | prefer an unsaturated peer when choosing first; zero means "prefer someone else", never "unavailable" |

The patterns are replaced wholesale on each hello, so a peer that unfollows can say so, and an advertisement older than
`kHelloStaleIntervals` hello intervals is treated as unknown - a worker that died last week must stop being ranked. Over-long
lists are truncated and counted (`patterns_truncated`) rather than refused, because a hostile follow set must not be able to hide
the rest of a good hello.

**Both fields are hints.** Delivery is decided by the sender's matcher and the receiver's subscription; acceptance by signatures
and channel rules; peer acceptability by CURVE pins. Nothing in that path reads an advertisement, and a peer that lies about its
patterns loses messages rather than gaining any (spec: `proto/peer-advertisement`).

## Versioning
`kProtocolVersion = 2` carried in `HelloMsg` (HELLO-1). Version 2 added the capacity byte, which is a wire change: a version 1
hello cannot be told from one that advertised nothing, so the version number is how the decoder refuses to guess. Unknown message ids are ignored, not errors (ENC-2). A breaking
change bumps the version and ships behind negotiation. Do not renumber ids.

## Wire encoding (ENC-1, ✅ implemented in `proto/codec.h`)

Framing: `u32 payload_size | u8 message_id | payload`, where `payload_size` counts the id byte and the payload. Records on
the wire are `canonical_bytes | u32 sig_size | sig`, so a receiver recomputes the hash from the bytes it actually received.

- **Ids are permanent**: 1 RecordMsg, 2 HeadMsg, 3 FetchMsg, 4 DirectRecordMsg, 5 HelloMsg, 6 AckMsg, 7 CreditMsg,
  8 GetHeadsMsg, 9 DirectHeadMsg; 128+ is experimental. Messages that are defined but unimplemented already own their id, so
  implementing one later is additive. A test pins the table.
- **Bounded before allocation**: a frame longer than `kMaxFrameBytes` is rejected from its header alone, and every inner
  length is validated against the bytes remaining.
- **Total decoder**: `decode_message` returns a status (`Ok`, `Truncated`, `Oversized`, `UnknownId`, `Malformed`,
  `UnsupportedVersion`) and never throws, reads out of bounds or yields a partial value. Unknown ids are ignored and counted;
  an unsupported version is reported rather than decoded.
- **The format is pinned** by committed golden vectors, so an encoder change fails the suite instead of silently moving the
  wire. Fuzzing is a seeded mutation test in the fast loop plus an opt-in libFuzzer target in `fuzz/`.
