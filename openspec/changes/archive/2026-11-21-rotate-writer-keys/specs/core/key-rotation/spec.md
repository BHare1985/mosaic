# Spec Delta

## Purpose

Defines how a writer hands its log to a successor key without breaking the chain of provenance, and the limits that keep the
mechanism from becoming a way to claim someone else's identity.

## ADDED Requirements

### Requirement: A rotation link is a signed record in the old log

A writer MUST announce a successor by publishing a record in its own log on the same channel, signed with the old key, whose
content type is `mosaic/rotation` and whose payload names the successor public key and the sequence number at which the
successor's log begins. The link MUST be an ordinary record: hash-chained, signed, replicated and stored like any other, so a
replica that does not understand rotation keeps it and a replica that does understands it can prove it.

#### Scenario: A rotation link is stored and replicated like any record

- **GIVEN** a writer that publishes a rotation link
- **WHEN** a follower that ignores rotation replicates the log
- **THEN** the link is present in the follower's store at its sequence number, unmodified

#### Scenario: A forged link does not verify

- **GIVEN** a rotation record signed with a key other than the log's writer key
- **WHEN** it is ingested
- **THEN** it is refused as a bad signature, exactly like any other forged record

### Requirement: Chains are followed only from a trusted root and only a bounded distance

A verifier MUST follow a rotation chain only when it already trusts the root identity, MUST follow at most a configured number of
links, and MUST refuse a chain that revisits an identity. A chain MUST be useless on its own: possessing a link from A to B does
not make B trusted unless A was trusted first.

#### Scenario: A stranger cannot adopt an identity

- **GIVEN** a node that trusts writer A, and an unrelated writer B that publishes a link claiming to succeed A
- **WHEN** records from B arrive
- **THEN** they are not attributed to A, because the claim is in B's log, not A's

#### Scenario: A cycle is refused

- **GIVEN** a chain where A rotates to B and B rotates back to A
- **WHEN** the chain is resolved
- **THEN** resolution stops at the repeated identity and the chain is reported as refused

#### Scenario: Depth is bounded

- **GIVEN** a chain longer than the configured limit
- **WHEN** it is resolved
- **THEN** resolution stops at the limit and the result is "not trusted", never "trusted by default"

### Requirement: Rotation does not rewrite history

A rotation link MUST NOT alter, reorder or invalidate records already in the old log. Records before the link MUST still verify
against the old key, and a fork in either log MUST be reported exactly as it is today.

#### Scenario: Old records stay valid

- **GIVEN** a log with records 0..5 and a rotation link at 6
- **WHEN** the store is verified after the rotation
- **THEN** records 0..5 still verify and the link verifies at 6

#### Scenario: A fork after rotation is still caught

- **GIVEN** a successor that equivocates on its second record
- **WHEN** two replicas compare
- **THEN** fork evidence is recorded for the successor's log, as it would be for any writer
