# core/record-integrity Specification

## Purpose
Defines the immutable shape of a log record - canonical bytes, hash, chain link and signature relationship - so any holder of a record can verify it independently of who stored or forwarded it.

## Requirements

### Requirement: Records have exactly one canonical encoding

Every record MUST encode to a single unambiguous byte sequence: a format version byte first, integers big-endian, every variable-length field preceded by a 32-bit length prefix. The signature MUST be excluded from those bytes. Equal field values MUST produce byte-identical output on every platform.

#### Scenario: Equal values encode identically

- **WHEN** two records hold the same writer, channel, sequence, link, timestamp, content type and payload
- **THEN** their canonical bytes are byte-identical

#### Scenario: Field boundaries cannot be shifted

- **WHEN** a payload is crafted to end with bytes that look like a following length prefix
- **THEN** decoding still recovers the original field values, because each length prefix delimits its field

#### Scenario: The signature is outside the hashed bytes

- **WHEN** a record's signature is replaced with a different value of the same length
- **THEN** the canonical bytes and the record hash are unchanged

### Requirement: A record hash covers every field except the signature

The record hash MUST be derived from the canonical bytes only, so it is stable across processes, restarts and platforms. Any change to writer, channel, sequence, predecessor link, timestamp, content type or payload MUST change the hash.

#### Scenario: Any field change changes the hash

- **WHEN** two records differ in exactly one of writer, channel, sequence, link, timestamp, content type or payload
- **THEN** their hashes differ

#### Scenario: Hash is reproducible

- **WHEN** the same record is hashed in a different process or after a restart
- **THEN** the hash is identical

### Requirement: Every record links to its predecessor

The link field of sequence 0 MUST be all zero bytes. For sequence greater than 0 the link MUST equal the hash of the immediately preceding record in the same log. A record whose link does not match the stored predecessor is evidence of a broken chain or of equivocation.

#### Scenario: Genesis record has an empty link

- **WHEN** a writer creates sequence 0
- **THEN** its link field is the all-zero hash

#### Scenario: Successor carries the predecessor hash

- **WHEN** a writer creates sequence n greater than 0
- **THEN** its link field equals the hash of sequence n-1 in the same log

### Requirement: A signature binds a record hash to a writer identity

A record's signature MUST cover exactly the record hash and nothing else, and MUST be checked against a writer identity carried in the record. The writer identity MUST be stable and derived from the signing key, so a restarted writer keeps ownership of its existing log instead of starting a new one.

#### Scenario: A valid signature verifies

- **WHEN** a record signed by a writer's key is verified against that writer's identity
- **THEN** verification succeeds

#### Scenario: Tampered content fails verification

- **WHEN** any hashed field of a signed record is altered after signing
- **THEN** verification fails

#### Scenario: Identity survives a restart

- **WHEN** a writer reloads its key material and continues an existing log
- **THEN** the writer identity is unchanged and earlier records still verify

### Requirement: Integrity is provided before authenticity

Hash-chaining MUST provide integrity - a stored prefix cannot be altered without detection - independently of signature strength. No capability, document or product claim MUST present the system as tamper-evident or authenticating while a non-cryptographic signer is in use.

#### Scenario: Chain tampering is detected without signatures

- **WHEN** a stored record's payload is modified and the chain is re-read
- **THEN** the break is detected from the hash link alone

#### Scenario: Weak signers are not advertised as security

- **WHEN** the implementation uses a non-cryptographic signer
- **THEN** product documentation states that records are unauthenticated, and no capability depends on signature strength
