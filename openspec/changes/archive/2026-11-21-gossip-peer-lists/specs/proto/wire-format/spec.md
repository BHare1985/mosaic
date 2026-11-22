# Spec Delta

## Purpose

Defines the bytes on the wire: permanent message ids, a total decoder, golden vectors. This delta adds the peer-list message.

## ADDED Requirements

### Requirement: Peer lists are a framed, length-bounded message

`PeersMsg` MUST have a permanent message id, MUST encode a bounded number of entries - each with a node id, a public endpoint, a
router endpoint, a hop count and an age - and MUST be decoded totally: a truncated, oversized or malformed message yields a decode
status and no message. An entry that survives decoding is one a node may dial, so the decoder MUST refuse an endpoint that is not a
reachable `tcp://host:port`.

#### Scenario: Round-trip

- **GIVEN** a peer list with three entries
- **WHEN** it is encoded and decoded
- **THEN** the decoded message equals the original, including entry order and ages

#### Scenario: A truncated message is refused

- **GIVEN** an encoded peer list cut in half
- **WHEN** it is decoded
- **THEN** the decoder reports a truncation and returns nothing

#### Scenario: An endpoint that is a bind address is refused

- **GIVEN** a peer list entry whose endpoint is `tcp://0.0.0.0:5555` or `tcp://*:5555`
- **WHEN** it is decoded
- **THEN** the message is malformed, because a wildcard says where a node listens, not where it can be found

#### Scenario: A golden vector pins the layout

- **GIVEN** the golden vector for `PeersMsg`
- **WHEN** the encoder changes in a way that alters the bytes
- **THEN** the vector test fails
