# log — writing and verifying logs

**Purpose:** `LogWriter` creates the signed hash chain; `ReplicaLog` is the **only** place records from other
parties are verified (signature, chain, fork detection) before they reach a store.

**Files:** `log.h`.

**`IngestResult` is the contract with Node:** `Accepted`, `Duplicate`, `Gap` (fetch it), `Fork` (evidence of an
equivocating writer), `BadChain`, `BadSignature`. Add new outcomes here, never as ad-hoc return values elsewhere.

**Rules**
- Depends on core and store only.
- Verify before store. Never store an unverified record.

`ReplicaLog::ingest_batch` takes a run of records, verifies each, appends contiguous stretches with one
`append_batch` per log and reports an outcome per record (`NotAttempted` past the first refusal). A
repair reply therefore costs one store call, and a record whose predecessor was dropped by retention is
verified against a checkpoint instead (`set_origin`).
