# store — the persistence seam

**Purpose:** `LogStore` stores records of many logs, in order, and returns them. Nothing else.

**Files:** `log_store.h` (interface + contract), `memory_log_store.h` (reference implementation),
`sqlite_log_store.h` (durable backend, `MOSAIC_WITH_SQLITE`).

**Suites:** `run_store_conformance(factory)` in `test/source/store_conformance.h` - every backend runs through it,
so adding one costs a single test case.

**Rules**
- A store never verifies anything (no hashing, no signatures). `log/ReplicaLog` does that. This keeps every backend
  behaviourally identical.
- `append` only accepts `seq == head+1` (or 0). Everything else returns `NotNext`.
- Any new backend must pass the shared conformance suite (STORE-0) before merging.

**Durability and its cost (STORE-1).** `SqliteLogStore` runs WAL with `synchronous=FULL`, one transaction per append, so
"append reported Ok" means the record is on disk. That costs a flush per record:

```
SqliteLogStore: 5000 durable appends in 4094225us (818us each)   # ~1200 appends/s
command: ./build/test/MosaicTests -tc="*throughput*"
```

That number is the price of the contract, not a defect. If a writer needs more throughput the answer is group commit or
`synchronous=NORMAL` (durability to the OS, not the platter) - a policy choice to make deliberately, with this measurement as
the baseline, not a flag to flip in passing.

**Open work:** STORE-2 (segment files), STORE-3 (retention + signed checkpoints).
