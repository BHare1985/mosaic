# Spec Delta

## Purpose

A store holds append-only logs and never interprets them. This delta makes forgetting part of the contract: a log may be
truncated at its head, and what remains must still be verifiable and still be a log.

## MODIFIED Requirements

### Requirement: Appends must be contiguous

An append MUST follow the head of its log. A store MAY have lost a prefix to retention, in which case its origin is the sequence
and hash recorded in a checkpoint, and appends MUST continue from the head. A store MUST refuse an append that skips a sequence
and MUST report the sequence it expected.

#### Scenario: Next sequence is accepted

- **WHEN** a record with sequence equal to head plus one is appended
- **THEN** the append succeeds and the head advances to that sequence

#### Scenario: A gap in the append is rejected

- **WHEN** a record with sequence greater than head plus one is appended
- **THEN** the append is rejected and the head is unchanged

#### Scenario: An append after truncation continues the log

- **GIVEN** a log whose records 0..99 were dropped and whose checkpoint records sequence 99
- **WHEN** record 100 is appended
- **THEN** it is accepted, and reads from 100 return it

## ADDED Requirements

### Requirement: A truncated log stays verifiable from its checkpoint

A store MUST support dropping a prefix of a log in exchange for a signed checkpoint that names the last dropped sequence and its
hash. After truncation, records MUST remain contiguous from the checkpoint, the checkpoint MUST be retrievable, and verification
of a later record MUST be able to establish descent from the checkpoint rather than from a record that is gone.

#### Scenario: Truncated replica still verifies new records

- **GIVEN** a replica that truncated records 0..99 with a checkpoint at 99
- **WHEN** record 100 arrives and is verified
- **THEN** verification succeeds when 100's predecessor hash equals the checkpoint's hash, and fails otherwise

#### Scenario: Truncation never discards the checkpoint

- **GIVEN** a store that has truncated further than a previously stored checkpoint
- **WHEN** the store is reopened
- **THEN** the newest checkpoint is still present and reads still start at its successor

### Requirement: Retention is bounded by replication and policy, never by whim

A store MUST drop records only when every configured condition is met: the record is beyond the age or size limit, and (when
`min_replicas` is set) enough replicas have acknowledged it. A store MUST NOT drop a record whose retention is unknown, and MUST
NOT drop a prefix that would leave a checkpoint it cannot sign or verify.

#### Scenario: Unreplicated records are kept

- **GIVEN** `min_replicas = 2`, one acknowledgement, and an age limit that has passed
- **WHEN** retention runs
- **THEN** nothing is dropped

#### Scenario: Replicated and expired records are dropped

- **GIVEN** `min_replicas = 2`, two acknowledgements, and records older than the age limit
- **WHEN** retention runs
- **THEN** the eligible prefix is dropped, a checkpoint is written, and the store reports how many records it dropped
