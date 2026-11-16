# Spec Delta

## ADDED Requirements

### Requirement: A durable store commits before reporting success

A store that claims durability MUST have committed an append before reporting success, so a record that a caller believes is
stored survives an immediate crash. A non-durable store MUST document that it makes no such promise.

#### Scenario: Success means committed

- **WHEN** an append reports success on a durable store and the process is killed immediately
- **THEN** the record is present after reopening

#### Scenario: Failure means not committed

- **WHEN** an append reports failure
- **THEN** the log's head is unchanged after reopening

### Requirement: A reopened store continues the same log

A reopened store MUST report the same heads it reported before closing, so a writer resumes at head plus one and a replica
resumes verification at the right sequence instead of re-ingesting or stalling.

#### Scenario: Head survives reopen

- **WHEN** a store holding logs up to sequence 99 is closed and reopened
- **THEN** each head is still 99 and the next append at 100 succeeds

#### Scenario: A writer resumes without renumbering

- **WHEN** a writer restarts against its durable store
- **THEN** the next published record continues the sequence and links to the stored head

### Requirement: A damaged tail never makes a log unreadable

If the last write was torn by a crash, a store MUST recover to the last complete record rather than failing to open or serving a
partial record. Records before the tear MUST remain readable and verifiable.

#### Scenario: Torn tail is truncated, not served

- **WHEN** a store is opened after a simulated torn write at the tail
- **THEN** it opens, reports the head of the last complete record, and every readable record still verifies

#### Scenario: Damage does not spread backwards

- **WHEN** the tail is discarded during recovery
- **THEN** earlier records are bit-identical to before the crash
