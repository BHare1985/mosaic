# Spec Delta

## Purpose

Defines how the replication protocol is carried over ZeroMQ between real processes, including what the socket layer is trusted to decide and what it is not.

## ADDED Requirements

### Requirement: Live traffic and repair unicast use separate socket kinds

Live publication MUST use a publish/subscribe pattern addressed by channel, and repair traffic MUST use a request-capable
unicast pattern addressed by node identity. A node MUST be able to receive live traffic from any connected peer while sending
repair requests to a specific one.

#### Scenario: Repair reaches exactly one peer

- **WHEN** a follower requests a missing range from one peer
- **THEN** only that peer receives the request and only that peer responds

#### Scenario: Live traffic reaches all matching peers

- **WHEN** a writer publishes on a channel three peers follow
- **THEN** all three receive the record

### Requirement: Wildcard matching is decided locally, never by the socket layer

A subscription MUST subscribe to the literal prefix of the pattern up to its first wildcard, so the socket layer can pre-filter
coarsely. The full pattern MUST still be re-checked against every received channel, because the socket layer matches byte
prefixes and cannot express "exactly one segment".

#### Scenario: A star pattern does not admit a different depth

- **WHEN** a node follows storage.*.shard7 and a peer publishes on storage.eu.frankfurt.shard7
- **THEN** the record is not stored, even though its byte prefix matched the subscription

#### Scenario: The root pattern receives everything

- **WHEN** a node follows > and any channel is published
- **THEN** the record arrives and is matched by the local pattern check

### Requirement: Queue limits are explicit and overflow is a counted drop

Socket queue limits MUST be configured explicitly rather than inherited, and overflow on the live path MUST result in dropped
messages that are counted, never in a blocked publisher. Repair traffic MUST remain independent of live overflow.

#### Scenario: A stalled follower does not block the writer

- **WHEN** one follower stops reading while publication continues
- **THEN** the writer keeps publishing, the excess is dropped and counted, and the follower repairs afterwards

#### Scenario: Repair still works after live overflow

- **WHEN** a follower has lost live records to overflow
- **THEN** it fetches the missing range and converges

### Requirement: Links are encrypted and the server is authenticated

Every connection MUST be CURVE-encrypted, and a client MUST verify the key of the server it connects to. A node MUST NOT
require a pre-provisioned or central membership list to accept a connection: data authority comes from record signatures, so a
link key never decides what is true.

#### Scenario: An unknown but properly authenticated key can join

- **WHEN** a peer whose key the node has never seen connects to an endpoint it is configured to accept
- **THEN** the connection is encrypted, established, and its key is recorded for that endpoint

#### Scenario: A server that is not the expected one is refused

- **WHEN** a client is configured with the wrong server key for a peer
- **THEN** the handshake fails and no message crosses the link

### Requirement: Peer keys are pinned where observable, and the gap is counted

Where the transport can observe a peer's authenticated key, it MUST pin it per endpoint on first use and refuse a later
change. Where it cannot, it MUST count the connection as unauthenticated rather than claim a check it did not perform. Closed
mode MUST refuse peers that are not explicitly listed.

#### Scenario: A pinned identity cannot change silently

- **WHEN** a peer connects to an endpoint it has used before but presents a different key
- **THEN** the connection is refused and counted, once enforcement is possible

#### Scenario: An unobservable key is reported, not assumed

- **WHEN** a message is accepted over CURVE without an observable peer key
- **THEN** it is counted as unauthenticated, so the missing enforcement is measurable

#### Scenario: Closed mode refuses keys outside the list

- **WHEN** a node runs in closed mode and a peer presents a key that is not on the allow-list
- **THEN** the connection is refused before any message is exchanged

### Requirement: CURVE keys derive from the writer identity and stay interoperable

The transport MUST derive its CURVE key pair from the node's Ed25519 identity (curve25519 conversion of the signing key) and
present it in the z85 form ZeroMQ expects, so the identity that authenticates a link is the identity that signs records. It MUST
import and export the ecosystem's certificate file format (the czmq certificate container) so existing tooling can be used,
without that file ever becoming the source of truth.

#### Scenario: Link identity is the record identity

- **WHEN** a node authenticates a link and then delivers a record it signed
- **THEN** the key that authenticated the link and the writer identity on the record derive from the same keystore entry

#### Scenario: An existing certificate imports

- **WHEN** a certificate file produced by the ecosystem's tooling is imported
- **THEN** the resulting identity is usable both for CURVE on links and for signing records

#### Scenario: Export is readable by the ecosystem

- **WHEN** a node exports its identity in that certificate format
- **THEN** the ecosystem's tooling accepts the file and derives the same public identity
