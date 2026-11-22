# store/log-store Specification

## Purpose
Defines append-only persistence of logs by sequence, so the log layer can replicate and verify without knowing where bytes live.

## Requirements

### Requirement: A store holds append-only logs keyed by writer and channel

A store MUST hold independent logs identified by the pair of writer identity and channel. Each log is an ordered sequence starting at 0 with no gaps. The store never merges, reorders or rewrites an entry.

#### Scenario: Two writers keep separate logs on one channel

- **WHEN** two writers publish to the same channel and a store holds both
- **THEN** reading either log returns only that writer's records

#### Scenario: Stored records are immutable

- **WHEN** a record is appended at an occupied sequence
- **THEN** the append is rejected and the stored record is unchanged

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

### Requirement: Reads are ordered, bounded and never invent data

A read MUST return at most the requested count of records in ascending sequence order, starting at the requested sequence and never past the head. A request beyond the head MUST return the available prefix or nothing, never a placeholder.

#### Scenario: Bounded read returns an ordered window

- **WHEN** a caller reads 10 records from sequence 5 of a log whose head is 20
- **THEN** exactly 10 records are returned, ordered by sequence, beginning at 5

#### Scenario: Read past the head is empty

- **WHEN** a caller reads from a sequence greater than the head
- **THEN** no records are returned and no error is raised

#### Scenario: Unknown log reads as empty

- **WHEN** a caller reads from a writer and channel the store has never seen
- **THEN** the result is empty and the log reports no head

### Requirement: The store never verifies, hashes or signs

Storage MUST treat records as opaque values with sequence numbers. Signature and chain verification belong to the log layer, so every store implementation stays interchangeable and byte-agnostic.

#### Scenario: A forged record can be stored

- **WHEN** a structurally valid record with an invalid signature is appended at the next sequence
- **THEN** the store accepts it, and rejection happens in the log layer above

### Requirement: Stores can be enumerated

A store MUST report the writer and channel of every log it holds, so replicas can be repaired and verified without external bookkeeping.

#### Scenario: Enumeration lists every retained log

- **WHEN** three logs have been written to one store
- **THEN** enumeration reports exactly those three writer and channel pairs

### Requirement: In-memory storage promises no durability

A store that does not explicitly claim durability MUST NOT be described as durable, and no caller MUST rely on its records surviving the process. Durability is added only by a store that states it commits before reporting success.

#### Scenario: Process end loses non-durable records

- **WHEN** a node runs against a non-durable store and the process ends
- **THEN** nothing claims those records were persisted, and a fresh store starts empty

#### Scenario: Durability is claimed per store, not implied by the contract

- **WHEN** a caller reads the store contract
- **THEN** durability is stated as a property of a specific implementation, not of the interface

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
