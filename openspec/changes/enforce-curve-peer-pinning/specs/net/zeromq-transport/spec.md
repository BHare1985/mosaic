# Spec Delta

## MODIFIED Requirements

### Requirement: Peer keys are pinned where observable, and the gap is counted

A node MUST decide whether to accept a peer's key before any message is exchanged, using the policy it was configured with: open
mode pins a key on first use and refuses a later change, closed mode refuses keys that are not listed. A refusal MUST be visible
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
