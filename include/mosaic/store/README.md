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

**Durability is a named choice (STORE-1, MEASURE-1).** `SqliteLogStore::open(path, Durability)`:

| Mode | PRAGMA | A crash can lose | Measured (2000 appends) |
|---|---|---|---|
| `PerRecord` (default) | `synchronous=FULL` | nothing that was reported as written | 648 µs each |
| `Batched` | `synchronous=NORMAL` | the tail of what a process had written, on power loss | 49 µs each |

`append_batch` puts a whole range in one transaction, which is where the real win is:

| Path | Measured |
|---|---|
| 2000 records, one `append` each, `PerRecord` | 1,296,527 µs (648 µs each) |
| 2000 records, one `append` each, `Batched` | 99,335 µs (49 µs each) |
| 2000 records in 500-record batches, `PerRecord` | 9,976 µs (5.0 µs each) |
| 2000 records in 500-record batches, `Batched` | 3,830 µs (1.9 µs each) |

The default is the safe one, and it stays the default: `Batched` is opt-in and the table is what an operator chooses
from. The numbers come from `./build/test/MosaicTests -tc="*throughput*,*batch append*"` and are re-runnable; they were
recorded rather than asserted, because a wall-clock threshold on a shared machine is a flaky test until it is an order of
magnitude.

**Open work:** STORE-2 (segment files), STORE-3 (retention + signed checkpoints).
