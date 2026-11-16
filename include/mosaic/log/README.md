# log — writing and verifying logs

**Purpose:** `LogWriter` creates the signed hash chain; `ReplicaLog` is the **only** place records from other
parties are verified (signature, chain, fork detection) before they reach a store.

**Files:** `log.h`.

**`IngestResult` is the contract with Node:** `Accepted`, `Duplicate`, `Gap` (fetch it), `Fork` (evidence of an
equivocating writer), `BadChain`, `BadSignature`. Add new outcomes here, never as ad-hoc return values elsewhere.

**Rules**
- Depends on core and store only.
- Verify before store. Never store an unverified record.

**Open work:** LOG-1 (keep fork evidence), LOG-2 (batch ingest).
