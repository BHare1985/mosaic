# proto/wire-format Specification

## Purpose
Defines how records and protocol messages become bytes on a link, so that two independent implementations of the same protocol version interoperate and a decoder can consume hostile input without risk.

## Requirements

### Requirement: Every message carries a permanent numeric id

Each message type MUST have a single one-byte identifier that is never reused, never renumbered and never reassigned to a
different meaning. Identifiers for messages that are defined but not yet implemented MUST be reserved before the first wire
release, so implementing them later is not a wire break.

#### Scenario: An id keeps its meaning across versions

- **WHEN** a protocol version is added after the first wire release
- **THEN** every existing message keeps its original identifier

#### Scenario: Reserved ids arrive later without renumbering

- **WHEN** a message whose id was reserved at version 1 is implemented
- **THEN** it appears on the wire under its reserved id and older peers can ignore it by id

### Requirement: Messages are length-framed and bounded before decoding

Each frame MUST state its own size before its content, MUST be rejected if it exceeds the configured maximum frame size, and
MUST be rejected if its declared length disagrees with the bytes actually available. No decoder may allocate based on an
unvalidated length.

#### Scenario: An oversized frame is rejected before allocation

- **WHEN** a frame declares a length above the maximum frame size
- **THEN** it is rejected without allocating that amount of memory

#### Scenario: A truncated frame is rejected

- **WHEN** a frame declares more bytes than the link supplied
- **THEN** decoding fails and no partial message is produced

### Requirement: Decoding hostile input is total and safe

Decoding MUST never read out of bounds, never invoke undefined behaviour and never throw. Every failure MUST be reported as a
normal decode failure to the caller. Every length prefix inside a message MUST be validated against the remaining bytes before
it is used.

#### Scenario: Corrupted bytes produce a failure, not a crash

- **WHEN** arbitrary bytes are fed to the decoder
- **THEN** the result is either a well-formed message or a decode failure, and the decoder never reads outside its buffer

#### Scenario: Inner lengths are validated

- **WHEN** a message's inner length prefix claims more bytes than the frame holds
- **THEN** decoding fails for that message

### Requirement: A record travels as its canonical bytes plus its signature

On the wire a record MUST be its canonical encoding followed by the signature as a length-prefixed field, so that any holder
can recompute the hash from the bytes it received and verify without trusting the sender's framing. Signature length MUST be
validated like any other length.

#### Scenario: Received bytes reproduce the hash

- **WHEN** a record is encoded, transmitted and decoded
- **THEN** the recomputed hash from the received canonical bytes equals the sender's hash

#### Scenario: A record with a malformed signature field is rejected

- **WHEN** the signature length prefix does not match the remaining bytes
- **THEN** the record is rejected as undecodable, not verified against a truncated signature

### Requirement: Encoding is deterministic and round-trips

Encoding MUST be deterministic for equal values, and decode-then-encode MUST reproduce the original bytes exactly for every
message in the set. The set MUST be covered by round-trip tests, golden vectors checked into the repository, and a fuzz test.

#### Scenario: Round trip preserves bytes

- **WHEN** every message type is encoded, decoded and re-encoded
- **THEN** the second encoding is byte-identical to the first

#### Scenario: Golden vectors pin the format

- **WHEN** the encoder is changed in a way that alters bytes
- **THEN** the committed golden vectors fail, so the wire format cannot change silently

### Requirement: Unknown identifiers and unsupported versions are handled explicitly

An identifier that is not known to the receiver MUST be ignored and counted, not treated as an error. A protocol version the
receiver does not support MUST be reported to the caller rather than decoded on a best-effort basis.

#### Scenario: An unknown message does not break the link

- **WHEN** a frame with an unassigned identifier arrives
- **THEN** it is discarded, counted, and the session continues

#### Scenario: A version mismatch is reported

- **WHEN** a peer announces a protocol version the receiver does not support
- **THEN** the mismatch is reported and no messages of that version are decoded

### Requirement: Version negotiation is announced and retried

A node MUST announce its protocol version to every peer it can address, and MUST keep retrying until that peer answers,
because a hello can be lost like any other message.

#### Scenario: Retry until answered

- **WHEN** a peer has not announced a version
- **THEN** the node keeps announcing its own, at the announcement interval, and stops once a version arrives

#### Scenario: Matching versions proceed

- **WHEN** both peers announce a supported version
- **THEN** payload exchange proceeds and nothing is counted as unnegotiated

### Requirement: Payload from an unsupported version is refused

Payload from a peer that announced a version the receiver cannot speak MUST be refused and counted. Payload from a peer whose
version is not yet known MUST be counted as unnegotiated and MAY be processed: records are self-verifying, so stalling
replication to enforce a metadata handshake would cost convergence and buy no security.

#### Scenario: Payload before hello is counted, not fatal

- **WHEN** a record arrives from a peer that has not yet announced a version
- **THEN** the record is processed and counted as unnegotiated, and the handshake keeps retrying

#### Scenario: An unsupported version is refused

- **WHEN** a peer announces a version the receiver cannot speak and then sends payload
- **THEN** the payload is refused, counted, and never stored or delivered
