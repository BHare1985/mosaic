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

### Requirement: Every connection is authenticated before it carries log data

Connections MUST be encrypted and authenticated with CURVE, and a peer whose public key is not on the allow-list MUST NOT be
able to connect or exchange any message. A node's transport identity and its record-signing identity MUST be the same key, so
authenticating a link authenticates the writer behind it.

#### Scenario: An unlisted key cannot connect

- **WHEN** a peer with a public key absent from the allow-list attempts to connect
- **THEN** the connection is refused and no message is exchanged

#### Scenario: Identity agrees between link and log

- **WHEN** a node is addressed by its identity and sends signed records
- **THEN** the identity used to authenticate the link derives from the same key that signed the records

### Requirement: ZeroMQ stays confined to one target

No module outside the ZeroMQ transport MUST include a ZeroMQ header, and the core library MUST build and pass its tests with no
ZeroMQ present. The transport MUST be an implementation of the transport contract, verified by the same conformance suite as any
other implementation.

#### Scenario: The core builds without ZeroMQ

- **WHEN** the project is built with no ZeroMQ installed
- **THEN** the core library and its tests build and pass

#### Scenario: The transport passes the shared conformance suite

- **WHEN** the ZeroMQ transport runs the transport conformance suite over loopback
- **THEN** it passes without transport-specific exceptions in the suite

### Requirement: Two processes converge over a real link

Two nodes in separate processes MUST converge: a follower reaches the writer's head and keeps up, and convergence MUST hold
after a link loss that is repaired.

#### Scenario: Follower reaches the writer's head

- **WHEN** two processes are configured as peers and one publishes 500 records
- **THEN** the other holds all 500 in order

#### Scenario: Reconnect repairs the gap

- **WHEN** a peer link is broken during publication and later restored
- **THEN** the follower converges to the writer's head without operator action

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
