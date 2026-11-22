# net/zeromq-transport Specification

## Purpose
Defines how the replication protocol is carried over ZeroMQ between real processes, including what the socket layer is trusted to decide and what it is not.

## Requirements

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

A node MUST decide whether to accept a peer's key before any message is exchanged, using the policy it was configured with. Open
mode pins a key on first sight of a peer address and refuses a later change from that address; closed mode refuses any presented
key that is not one of the provisioned keys. Where the handshake exposes no peer identity - ZeroMQ's does not - the decision MUST
NOT depend on one. A refusal MUST be visible
to the operator. A node MUST NOT accept an encrypted peer whose key it could not verify.

#### Scenario: A pinned identity cannot change silently

- **WHEN** a peer connects to an endpoint it has used before but presents a different key
- **THEN** the handshake is refused before any message is decoded, and the refusal is counted

#### Scenario: Closed mode refuses keys outside the list

- **WHEN** a node runs in closed mode and a peer presents a key that is not on the allow-list
- **THEN** the handshake is refused and the peer is never added to the pins

#### Scenario: An unseen key is pinned in open mode

- **WHEN** a node in open mode is contacted by a peer it has not seen before
- **THEN** the handshake succeeds, the key is pinned for that peer, and later traffic must match it

#### Scenario: An unobservable key is reported, not assumed

- **WHEN** a message arrives over an encrypted link whose peer key was not established during the handshake
- **THEN** it is refused and counted, so an unverifiable peer never contributes a record

#### Scenario: Nothing is accepted unverified

- **WHEN** any encrypted connection is established
- **THEN** its peer key has been decided by the pinning policy before the first message is decoded

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

### Requirement: The poll set is sized for the peers it watches

The set of sockets a transport polls MUST grow with the peers it is connected to. No fixed capacity may be assumed, because peers
arrive at runtime through configuration, discovery and gossip.

#### Scenario: Many peers do not overflow a fixed array

- **WHEN** a node is connected to more peers than any earlier fixed-size poll set could hold
- **THEN** polling watches every socket, delivers from all of them, and no memory outside the poll set is written

#### Scenario: Adding a peer mid-run is safe

- **WHEN** a peer is wired after the transport has already been polling
- **THEN** the next poll includes it, with no reallocation hazard and no lost subscription

### Requirement: A message is exactly the frames its kind declares

Each inbound path MUST expect a fixed number of frames - two for a unicast message (identity, payload), three for a live one
(channel, sender, payload) - MUST drain any surplus so the next message starts clean, and MUST refuse and count a message whose
frame count is wrong.

#### Scenario: A surplus frame does not become the next message

- **WHEN** a peer sends one more frame than its message kind declares
- **THEN** the message is refused and counted, and the following poll returns the peer's next real message rather than the
  surplus frame

#### Scenario: A short message is refused

- **WHEN** a multi-frame message is truncated after its first frame
- **THEN** nothing is delivered and the truncation is counted

### Requirement: Frame size and queue depth are bounded in bytes

The socket MUST refuse a frame larger than the wire format's limit before it is buffered, and the queue limit MUST be expressed
where an operator can see the resulting worst-case memory per peer. The documented bound MUST be a number the configuration
implies.

#### Scenario: An oversized frame never reaches the decoder

- **WHEN** a peer sends a frame above the wire format's maximum
- **THEN** the socket refuses it, the message is not buffered, and the refusal is counted

#### Scenario: The memory bound is derivable

- **WHEN** an operator reads the queue-limit setting and the frame limit
- **THEN** the product of the two is stated as the worst case per peer, and the documented default is that number

### Requirement: A silent link is detected, and a restarted peer is admitted

A transport MUST notice a peer that stops answering within a configured interval rather than waiting for TCP's default lifetime,
and MUST allow a peer that reconnects with an identity it already holds to take over its own connection.

#### Scenario: A dead link is noticed on a schedule

- **WHEN** a connected peer stops responding without closing the connection
- **THEN** the transport gives up on that link after the configured heartbeat interval and timeout, not at an OS default

#### Scenario: A restarted node reclaims its identity

- **WHEN** a peer restarts and reconnects with the identity it used before, while the transport still holds its previous
  connection
- **THEN** the new connection takes over that identity and traffic flows again without waiting for the old one to expire

#### Scenario: Sends do not queue into a peer that has not connected

- **WHEN** a peer has been wired but its connection has not completed
- **THEN** a send to it is dropped and counted rather than queued
