# Design

## Context

See proposal.md - Why. Constraints that shape this:

- `core`..`node` take standard-library dependencies only; third-party code must sit behind an interface and needs maintainer
  approval (`docs/design/conventions.md`). `Hasher`, `Signer` and `Verifier` already exist as those seams.
- No exceptions outside `jobs`, so the decoder reports failure by value.
- The record's canonical encoding is already frozen as "format byte 1"; the length-prefix helpers live in
  `source/core/record.cpp` and are private to it.
- Messages are `std::variant` values with no encoding at all, and four of them (`AckMsg`, `HelloMsg`, `CreditMsg`,
  `GetHeadsMsg`) are defined but unimplemented.

## Goals / Non-Goals

**Goals:**

- Records that fail for cryptographic reasons, not only for structural ones.
- A byte format with permanent ids, golden vectors and a decoder that survives hostile input.
- Version and algorithm tags that make later changes additive rather than breaking.

**Non-Goals:**

- No transport, no sockets, no framing per link, no compression, no encryption. CURVE is a transport concern.
- No key rotation (SEC-3), no authorization rules, no key derivation from passphrases. Rotation adds a record type and belongs
  with a design note.
- No pattern advertisement in Hello (HELLO-1's second half) - it exists to serve worker discovery, so it lands with jobs.
- No attempt at canonical ordering across processes beyond the existing encoder; one encoder, one implementation.

## Decisions

**D1 - Algorithm identity lives in the record's canonical bytes, not in configuration.**
Canonical bytes become `u8 format_version | u8 hash_algorithm | ...` for version 1. A record is therefore self-describing: a
verifier reads the algorithm from the bytes it has, not from a build setting.
*Alternatives:* algorithm implied by build configuration - rejected, verification of historical logs would depend on how the
binary was compiled; algorithm in a separate header field - rejected, it is one more thing to keep consistent.

**D2 - libsodium behind the existing seams, with a dependency-free build still supported.**
`MOSAIC_WITH_SODIUM` (default ON when found) selects the Ed25519 signer and BLAKE2b/SHA-256 hasher. With it OFF, `core` still
builds and tests with the reference SHA-256 and the test signer, preserving the rule that the core library has no
dependencies and keeping CI cheap. No libsodium type crosses a header boundary.
*Alternatives:* vendored Ed25519 - rejected: we would own the audit; always-link libsodium - rejected: breaks the
ZeroMQ-free, dependency-free core build and every test-only build.

**D3 - Frame layout: `u32 length | u8 message_id | payload`, with version carried by Hello only.**
Four bytes of framing header, one id byte, then the message. *Why not per-frame version:* it doubles the header for a value
that is fixed per session, and the requirement already forces a Hello handshake. *Trade-off:* a receiver must have seen Hello
before it can interpret payload, which the version requirement states explicitly.

**D4 - Message ids are allocated now for the whole set, implemented or not.**
`RecordMsg`=1, `HeadMsg`=2, `FetchMsg`=3, `DirectRecordMsg`=4, then `HelloMsg`=5, `AckMsg`=6, `CreditMsg`=7,
`GetHeadsMsg`=8, `DirectHeadMsg`=9, and 128+ is reserved for experiments. Implementing an unimplemented message later is then
purely additive, and an old peer ignores what it does not know by id.

**D5 - The decoder is a checked reader, not a validating parser per message.**
One bounds-checked reader type walks the buffer; each message decoder asks it for fields. Any short read fails the whole
decode. This makes "no overread" structural rather than a property of each decoder's diligence.
*Alternative:* per-field `if (pos + n > size)` checks - rejected: the one that is forgotten is the bug.

**D6 - Fuzzing is a seeded mutation test in the suite, with an optional libFuzzer target.**
The default suite mutates valid frames (bit flips, truncations, length rewrites) from a fixed seed and asserts the decoder
only ever returns a value or a failure. A libFuzzer target is opt-in because it needs clang.
*Why:* the guard must run in the fast loop on GCC; libFuzzer is the deeper, occasional instrument.

**D7 - The writer identity is the hex public key, and the keystore is a file, not a config value.**
`generate`/`load`/`save` with an atomic write (temp file then rename) and mode 0600. Identity is derived from the key, so log
identity survives restarts; a keystore path comes from the caller, never from a global.
*Alternative:* identity assigned once and stored beside the key - rejected: it can be edited, which breaks "identity is
content-derived" and silently orphans logs.

**D8 - `InsecureTestSigner` survives as an explicitly named test double.**
It stays for simulation tests that do not want key generation cost, but it is renamed to make its nature unmissable, is not
reachable from the public convenience headers, and every capability that depends on authenticity is tested with real keys.

## Risks / Trade-offs

- **Changing canonical bytes invalidates the current test vectors** → Nothing has shipped on the wire, so this is the last
  cheap moment; the change is done in one step and the vectors are regenerated and re-committed as golden files.
- **libsodium availability differs per platform** → The build option plus the dependency-free fallback means no developer is
  blocked, and the full-featured CI job is the one that proves the real path.
- **Signature malleability / non-canonical signatures** → Ed25519 verification in libsodium rejects non-canonical signatures;
  the "another key cannot claim the identity" scenario is the regression test that it stays true.
- **A reserved id assigned to the wrong message** → Ids are asserted by a test that pins the full id table, so renumbering
  fails loudly.
- **Decoder fails open under a length rewrite** → The fuzz test asserts "value or failure", and golden vectors pin what a
  successful decode must look like.
- **CORE-2 scope creep into key management** → Keystore is generate/load/save only; rotation, passphrases and permissions
  policy beyond 0600 are explicitly out.

## Migration Plan

1. Framing, ids and encoder/decoder with golden vectors (no behaviour change to the node).
2. Algorithm-tagged canonical bytes; regenerate vectors.
3. Real hasher and Ed25519 behind the seams; keystore; real-key log tests.
4. Hello with version, and version refusal on the receive path.
5. Board cleanup and definition of done.

Rollback is per-step; step 2 is the only one that changes record bytes, and it precedes any use of them on a link.

## Open Questions

- **Hello enforcement needs a peer set the node does not have** (found during implementation, blocks task 4.1). The wire half
  is done: hello carries the version and an unsupported version is reported instead of decoded. But "reject payload from a
  peer that has not announced a version" requires knowing which peers exist and when a link starts - and `Transport` exposes
  neither (`self/subscribe/publish/send/poll` only, no peer list, no connect event). Today the node learns a peer only by
  receiving from it, so enforcement would have to be "reject until a hello arrives", which every simulation scenario would
  have to pay for without testing anything real (the simulator has no links to establish). The honest home for this is the
  transport change, where connections exist: it is recorded as a requirement there.

- Whether the message set needs a per-message minimum protocol version, or whether the single session version is enough.
  Deferrable: resolved by whoever first needs to add a message with a new field.
- Whether golden vectors should live as text (hex) or binary fixtures. Deferrable, no behavioural impact.
