# Proposal

## Why

The README's first sentence calls Mosaic *verifiable*, and the whole design rests on "anyone holding the head hash can verify
the prefix". Today that rests on `InsecureTestSigner` and a `std::variant` of message structs that never becomes bytes. So
the two claims the project is named for - a *signed, hash-chained* log, and a *replicated* one - are both unproven: the
signature cannot fail for a cryptographic reason, and nothing has ever crossed a process boundary.

This change makes the record layer genuinely verifiable and gives the message set a stable wire representation. It is the
prerequisite for the transport change: without a byte format there is nothing to put on a socket.

## What Changes

- Ed25519 signing and verification (libsodium behind the existing seam); the writer identity becomes the hex public key, so a
  restarted writer keeps its log instead of orphaning it.
- A keystore: generate, load and save key material with restrictive permissions, never in config files or logs.
- Hash algorithm identified by the format byte, so a future hash change does not require re-signing or breaking old logs.
- Binary encode/decode for the whole message set and for records on the wire, with a total decoder: bounded, no overreads, no
  exceptions, `std::optional` on failure, fuzz-tested.
- Message ids reserved now for the messages that are defined but unimplemented (`AckMsg`, `HelloMsg`, `CreditMsg`,
  `GetHeadsMsg`/`DirectHeadMsg`), so ids never need renumbering later.
- Hello carries the protocol version; a version mismatch is reported rather than silently decoded. Pattern advertisement in
  Hello moves to the jobs change, where a worker set first becomes observable.
- **BREAKING** for the on-wire format only: format version 1 of both record and message framing is fixed here; anything
  written before it did not exist outside a process.

## Capabilities

### New Capabilities

- `proto/wire-format`: the byte-level contract for records and messages - ids, framing, bounds, decoder totality, versioning
  and forward compatibility.

### Modified Capabilities

- `core/record-integrity`: the signature requirement becomes cryptographic (Ed25519 over the record hash, identity derived
  from the key), the hash requirement becomes algorithm-tagged, and the "integrity before authenticity" requirement is
  replaced by provided authenticity. Requires the baseline change `establish-capability-baseline` to be archived first, since
  it modifies an existing requirement rather than adding one.

## Impact

- `include/mosaic/core/crypto.h`, `source/core/crypto.cpp`: real hasher and signer/verifier behind the existing seams;
  `InsecureTestSigner` stays but becomes test-only and clearly named.
- `include/mosaic/core/record.h`, `include/mosaic/proto/messages.h`: encoding entry points; new codec in `source/proto/`.
- New dependency: **libsodium** (behind `Hasher`/`Signer`/`Verifier`, never in public headers). This is the one decision that
  needs explicit maintainer sign-off; the alternative is a vendored Ed25519 implementation, which trades a dependency for an
  audit surface we would own.
- `test/source/`: golden wire vectors, round-trip and fuzz tests, real-key log tests replacing insecure signer usage.
- `docs/design/TODO.md`: CORE-1, CORE-2, ENC-1, ENC-2 close; HELLO-1 splits, with only version negotiation closing here.
- Unblocks `two-machines-transport-and-durability`, which needs bytes, versions and identities to exist.
