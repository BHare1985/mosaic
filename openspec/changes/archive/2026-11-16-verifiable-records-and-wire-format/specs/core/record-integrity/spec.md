# Spec Delta

## MODIFIED Requirements

### Requirement: A record hash covers every field except the signature

The record hash MUST be derived from the canonical bytes only, so it is stable across processes, restarts and platforms. Any change to writer, channel, sequence, predecessor link, timestamp, content type or payload MUST change the hash. The hash algorithm MUST be identified by the format byte, so that a log written under one algorithm remains verifiable after the default changes and cannot be misread as another algorithm's log.

#### Scenario: Any field change changes the hash

- **WHEN** two records differ in exactly one of writer, channel, sequence, link, timestamp, content type or payload
- **THEN** their hashes differ

#### Scenario: Hash is reproducible

- **WHEN** the same record is hashed in a different process or after a restart
- **THEN** the hash is identical

#### Scenario: Old logs survive a hash change

- **WHEN** the default hash algorithm changes and a log written under the previous algorithm is verified
- **THEN** verification still succeeds because the algorithm is taken from the record's format byte

### Requirement: A signature binds a record hash to a writer identity

A record's signature MUST be a cryptographic signature (Ed25519) over exactly the record hash, verifiable against the writer
identity carried in the record and against nothing else. The writer identity MUST be the hex-encoded public key, so it is
content-derived, stable across restarts, and cannot be claimed by a different key.

#### Scenario: A valid signature verifies

- **WHEN** a record signed by a writer's key is verified against that writer's identity
- **THEN** verification succeeds

#### Scenario: Tampered content fails verification

- **WHEN** any hashed field of a signed record is altered after signing
- **THEN** verification fails

#### Scenario: Identity survives a restart

- **WHEN** a writer reloads its key material and continues an existing log
- **THEN** the writer identity is unchanged and earlier records still verify

#### Scenario: Another key cannot claim the identity

- **WHEN** a record is signed with a different private key but carries an identity derived from the original public key
- **THEN** verification fails

### Requirement: Integrity is provided before authenticity

Hash-chaining MUST provide integrity - a stored prefix cannot be altered without detection - independently of signature strength. Record authenticity is provided only by a cryptographic signature, and no capability, document or product claim MUST present the system as tamper-evident while a non-cryptographic signer is in use. Key material MUST be loaded from storage with restrictive permissions and MUST never appear in configuration, logs or error messages.

#### Scenario: Chain tampering is detected without signatures

- **WHEN** a stored record's payload is modified and the chain is re-read
- **THEN** the break is detected from the hash link alone

#### Scenario: Weak signers are not advertised as security

- **WHEN** the implementation uses a non-cryptographic signer
- **THEN** product documentation states that records are unauthenticated, and no capability depends on signature strength

#### Scenario: Key material is not readable by others

- **WHEN** a key is saved to storage
- **THEN** the file is created readable and writable only by its owner, and no code path prints the key or places it in configuration
