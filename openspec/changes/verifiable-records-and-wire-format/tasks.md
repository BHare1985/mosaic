# Tasks

## 1. Wire format skeleton (ENC-1)

- [ ] 1.1 Move the length-prefix helpers out of the record implementation into a shared encoding header used by both record
      and message codecs. Verified by: `test_core.cpp` canonical-bytes assertions unchanged and green.
- [ ] 1.2 Add a bounds-checked reader/writer pair whose every field read fails the whole decode on a short read.
      Verified by: a unit test feeding a buffer one byte short for each field type expecting failure, never a partial value.
- [ ] 1.3 Define the full message id table, including ids for the messages that are defined but unimplemented, and pin it in a
      test. Verified by: the pinning test fails if any id changes.
- [ ] 1.4 Encode and decode every message in the set with `u32 length | u8 id | payload`. Verified by: round-trip test over
      all message types, and a frame whose declared length disagrees with its bytes failing to decode.
- [ ] 1.5 Encode records on the wire as canonical bytes plus a length-prefixed signature; reject a mismatched signature
      length. Verified by: a record decoded from received bytes recomputes the sender's hash, and a truncated signature field
      fails to decode.
- [ ] 1.6 Commit golden vectors for one instance of each message and one record. Verified by: altering the encoder changes the
      vectors, so the test fails (spot-check on a scratch branch, do not commit).
- [ ] 1.7 Add the seeded mutation fuzz test asserting "value or failure" and no overread, plus an opt-in libFuzzer target.
      Verified by: the default suite runs the mutation test from a fixed seed in under a second on GCC.

## 2. Unknown ids and versions (ENC-2)

- [ ] 2.1 Ignore and count frames with an unassigned id without dropping the session. Verified by: a test feeding an unknown id
      between two valid messages, both of which are still processed.
- [ ] 2.2 Reject oversized frames and oversized inner lengths before allocation. Verified by: a test with a declared length
      above the maximum asserting no allocation and a decode failure.

## 3. Real hashing and signatures (CORE-1, CORE-2)

- [ ] 3.1 Add the `MOSAIC_WITH_SODIUM` build option selecting BLAKE2b/SHA-256 behind the hasher seam, keeping the reference
      SHA-256 as the dependency-free fallback. Verified by: both configurations build and pass; no libsodium type appears in a
      public header (`rg -n "sodium" include/mosaic` returns nothing).
- [ ] 3.2 Tag the hash algorithm in the canonical bytes ahead of the hash computation; regenerate the golden vectors.
      Verified by: two records differing only in algorithm tag hash differently, and a record written under the fallback
      verifies after switching the default.
- [ ] 3.3 Implement Ed25519 signer and verifier behind the seams, with writer identity derived as the hex public key.
      Verified by: `test_log.cpp` passes with real keys; a record signed by a different key carrying the original identity is
      rejected.
- [ ] 3.4 Add the keystore: generate, load, save with mode 0600 and an atomic rename. Verified by: a test asserting the file
      mode, a reload producing the same identity, and no key material appearing in any log or config path.
- [ ] 3.5 Demote `InsecureTestSigner` to an explicitly named test double that is unreachable from the convenience headers.
      Verified by: product sources do not reference it, and the tampering tests use real keys.
- [ ] 3.6 Add the end-to-end property that a signed record survives encode, transmit and decode with verification intact.
      Verified by: one test covering encode, decode, verify, and rejection when a byte in the canonical part is flipped.

## 4. Version handshake (HELLO-1, version part only)

- [ ] 4.1 Encode hello with the protocol version and reject payload messages from a peer that has not announced a supported
      version. Verified by: a test where a record before hello is refused, and one where matching versions proceed.
- [ ] 4.2 Record in `docs/design/02-protocol.md` that pattern advertisement in hello moved to the jobs change, and why.
      Verified by: the sentence exists and HELLO-1's board row reflects the split.

## 5. Close the board and prove the claim

- [ ] 5.1 Mark CORE-1, CORE-2, ENC-1, ENC-2 done and update the milestone rows; keep HELLO-1 partially open with a note.
      Verified by: `./scripts/check_todo_ids.sh` exits 0 and no `TODO(CORE-*|ENC-*)` remains in code.
- [ ] 5.2 Update `README.md` and `docs/design/07-security.md` so neither claims security before step 3 lands, and both claim
      it only once real keys are tested. Verified by: the status banner matches the build configuration actually shipped.
- [ ] 5.3 Run the definition of done: fast loop and ZeroMQ-stack builds, clang-format clean, no new dependency inside
      `core`..`node` beyond the approved libsodium seam, header contracts updated. Verified by: both builds green and the
      dependency rule checked by inspection of the module READMEs.

## Workflow follow-up

- `two-machines-transport-and-durability` consumes these bytes; re-read the message id table before writing the transport so it
  does not invent framing of its own.
- If libsodium approval is declined, this change's step 3 becomes a separate change with a vendored implementation and the wire
  format can still ship first.
