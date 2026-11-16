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

An append MUST succeed only for sequence equal to the log's current head plus one, and for sequence 0 when the log is absent. Any other sequence MUST be rejected as not-next, without modifying the log.

#### Scenario: Next sequence is accepted

- **WHEN** a record with sequence equal to head plus one is appended
- **THEN** the append succeeds and the head advances to that sequence

#### Scenario: A gap in the append is rejected

- **WHEN** a record with sequence greater than head plus one is appended
- **THEN** the append is rejected and the head is unchanged

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
