# core — vocabulary types, crypto seams, Record

**Purpose:** the types every other module speaks: `Bytes`, `Seq`, `Hash`, `LogKey`, `Record`, `Head`, and the
`Hasher` / `Signer` / `Verifier` interfaces.

**Files:** `types.h`, `crypto.h`, `record.h` (+ `source/core/`).

**Rules**
- No dependencies on other mosaic modules. No I/O, no clocks, no randomness.
- `canonical_bytes()` is a **frozen wire/hashing format**. Changing it changes every record hash. Any change needs a new
  format byte and a design doc update (01-data-model (`docs/design/01-data-model.md`)).
- `InsecureTestSigner` is a placeholder. It must never be used outside tests/demos.

**Open work:** CORE-1, CORE-2, CORE-3 in TODO (`docs/design/TODO.md`).
