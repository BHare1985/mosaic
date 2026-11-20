# Spec Delta

## Purpose

Defines the bytes on the wire: permanent message ids, a total decoder, golden vectors. This delta adds the peer-list message.

## ADDED Requirements

### Requirement: Peer lists are a framed, length-bounded message

`PeersMsg` MUST be part of the versioned message set with a permanent id, MUST encode a bounded number of entries, each carrying a
node id, a public endpoint, a router endpoint and an age, and MUST be decoded totally: a truncated, oversized or malformed message
MUST produce a decode status and no message, never a partial one.

#### Scenario: Round-trip

- **GIVEN** a peer list with three entries
- **WHEN** it is encoded and decoded
- **THEN** the decoded message equals the original, including entry order and ages

#### Scenario: A truncated message is refused

- **GIVEN** an encoded peer list cut in half
- **WHEN** it is decoded
- **THEN** the decoder reports a truncation and returns nothing

#### Scenario: A golden vector pins the layout

- **GIVEN** the golden vector for `PeersMsg`
- **WHEN** the encoder changes in a way that alters the bytes
- **THEN** the vector test fails
