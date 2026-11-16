# 04 — Flow control (🟡 repair path ✅, live path ⬜)

## Principles
1. **The receiver controls the rate.** It hands out credit; the sender spends it and never sends at zero.
2. **Drop, don't block.** One slow consumer must not slow producers or other consumers (Malamute's whitepaper, IP congestion
   model). Dropped live messages are recoverable because logs can be fetched.
3. **Bound everything**: in-flight records per peer, records per FETCH, bytes per record, queue sizes.

## Primitives (`flow/credits.h`)
- `CreditWindow(capacity, initial)`: `take_up_to(n)`, `grant(n)` (clamped), `reset()`.
- `CreditGranter(window)`: `initial()`, `on_consumed(n)` → batch refill when half the window is consumed.

## Where credit applies

| Path | Mechanism | Status |
|---|---|---|
| Repair (`FETCH`/`DIRECT_RECORD`) | Requester keeps a `CreditWindow` per peer (`fetch_window`). `FetchMsg.count` ≤ available; each `DirectRecordMsg` returns 1; a request that is abandoned on timeout returns exactly what it still owed. Responder also caps at `max_serve`. | ✅ |
| Live (`RecordMsg`) | Best effort; ZeroMQ HWM drops when a follower is slow. A follower that falls behind should *stop relying on live* and catch up by FETCH (slow-follower rule, FLOW-4). | ⬜ (FLOW-4) |
| Explicit grants (`CreditMsg`) | For streaming consumers that want push with back-pressure. | ⬜ FLOW-3 |
| Jobs | Workers advertise remaining capacity; ranking skips workers without credit. | ⬜ JOBS-4 |

## Credit accounting is per request (✅ FLOW-2)
Each outstanding FETCH carries the credit it spent (`FetchState::credit_left`). A request that completes or is abandoned on
timeout returns exactly what it still owed, and a response that arrives afterwards finds no live request, so it is consumed
without creating credit. The invariant the old blanket `reset()` broke:

```
credit_available(peer) == fetch_window - (credit_spent - credit_returned)
```

This holds after every step and is asserted in `test_node_sim.cpp` (`credit is accounted per request`). The blanket reset
let one late round inflate the next, so the node could hold more requests in flight than the window allows - unbounded
memory and duplicate traffic on a slow link. Verified by temporarily reintroducing the reset: the scenario fails.

## Interaction with ZeroMQ
HWM is a *safety net*, not the flow-control mechanism: it silently drops (PUB) or blocks (PUSH/DEALER). Our credits keep us
well under HWM. Set HWM explicitly in the ZeroMQ transport and test the "follower slower than writer" scenario in the sim.
