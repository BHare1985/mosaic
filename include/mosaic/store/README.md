# store — the persistence seam

**Purpose:** `LogStore` stores records of many logs, in order, and returns them. Nothing else.

**Files:** `log_store.h` (interface + contract), `memory_log_store.h` (reference implementation).

**Rules**
- A store never verifies anything (no hashing, no signatures). `log/ReplicaLog` does that. This keeps every backend
  behaviourally identical.
- `append` only accepts `seq == head+1` (or 0). Everything else returns `NotNext`.
- Any new backend must pass the shared conformance suite (STORE-0) before merging.

**Open work:** STORE-0..STORE-3.
