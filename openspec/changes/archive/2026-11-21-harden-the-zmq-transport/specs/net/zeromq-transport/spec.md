# Spec Delta

## ADDED Requirements

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
