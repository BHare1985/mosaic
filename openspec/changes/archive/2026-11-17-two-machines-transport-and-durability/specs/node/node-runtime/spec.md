# Spec Delta

## ADDED Requirements

### Requirement: A node can run as a pure replica

A node MUST be usable with no application callback at all, following patterns and holding logs for others while serving repair
requests. This makes a store-only process and a tail-only process meaningful without inventing a separate component.

#### Scenario: A replica holds logs it never delivers

- **WHEN** a node follows a broad pattern with no application callback configured
- **THEN** it stores every matching log, answers repair requests for those logs, and never invokes a callback

#### Scenario: A second follower repairs from the pure replica

- **WHEN** a follower starts after the writer has gone offline and requests records from the pure replica
- **THEN** the follower converges from the replica without contacting the writer

### Requirement: Handshake failures are observable

A node MUST report, as values a test or operator can read, how many peers announced a protocol version it cannot speak and how
many messages were accepted before a peer's version was known. On real links a version mismatch is otherwise indistinguishable
from a quiet peer.

#### Scenario: A mismatched peer is visible

- **WHEN** a peer announces a version the node cannot speak
- **THEN** the rejection counter increases and the peer's payload stops being stored or delivered

#### Scenario: Late negotiation is counted, not hidden

- **WHEN** records arrive before the handshake has completed
- **THEN** they are processed and counted as unnegotiated, so the condition can be seen rather than inferred
