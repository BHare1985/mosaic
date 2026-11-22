# log/replicated-log Specification

## Purpose
Defines single-writer append with a hash chain, and the follower-side verification that decides whether an arriving record is stored, ignored as a duplicate, or rejected as gap, fork or forgery.

## Requirements

### Requirement: A writer assigns contiguous sequences and signs every record

A writer MUST own one append-only log per channel and assign sequence numbers 0, 1, 2, ... with no gaps and no reuse. Each record MUST be linked to its predecessor and signed before it becomes readable. A writer that resumes from existing storage MUST continue at head plus one rather than restarting the sequence.

#### Scenario: Sequence numbers are contiguous from zero

- **WHEN** a writer publishes five records on one channel
- **THEN** the stored sequences are 0 through 4 with no gaps

#### Scenario: A resumed writer continues the log

- **WHEN** a writer restarts against storage whose head is 41
- **THEN** the next published record has sequence 42 and links to the stored head

### Requirement: Verification returns exactly one outcome per record

Ingesting a record MUST return one of accepted, duplicate, gap, fork, bad signature or bad chain. The decision MUST follow a fixed order - signature first, then comparison against stored history, then gap detection, then link check - so that every implementation classifies the same input identically.

#### Scenario: Verification order is fixed

- **WHEN** a record both carries an invalid signature and would extend a gap
- **THEN** the outcome is bad signature, not gap

#### Scenario: A well-formed next record is accepted

- **WHEN** a correctly signed record arrives with sequence equal to the stored head plus one and a matching link
- **THEN** the outcome is accepted and the record is stored

### Requirement: Missing records are never stored or delivered

A record arriving beyond the next expected sequence MUST be rejected as a gap and MUST NOT be stored or passed to the application. The log only fills forward, so no later record can hide a missing one.

#### Scenario: Out-of-order record is reported as a gap

- **WHEN** a record with sequence greater than the expected next sequence arrives
- **THEN** the outcome is gap, storage is unchanged and the application is not notified

### Requirement: Conflicting history is reported as a fork

A validly signed record that contradicts stored history MUST NOT overwrite it. Contradiction means a different record already stored at that sequence, or a link that does not match the stored predecessor. The conflict MUST be reported to the application.

#### Scenario: Two different records at one sequence

- **WHEN** a second, differently hashed record arrives for a sequence that is already stored
- **THEN** the outcome is fork, the stored record stays, and a fork callback fires

#### Scenario: A link that contradicts stored history

- **WHEN** the next record's link does not equal the hash of the stored predecessor
- **THEN** the outcome is fork and the record is not stored

#### Scenario: Replay of an identical record is harmless

- **WHEN** the exact record already stored at a sequence arrives again
- **THEN** the outcome is duplicate and the application is not notified twice

### Requirement: Application delivery is once per record, in order

A follower MUST deliver each accepted record to the application exactly once, in sequence order, per followed log. No record may be delivered before it is verified and stored.

#### Scenario: Duplicate delivery does not reach the application

- **WHEN** the same record is received three times
- **THEN** the application sees it once

### Requirement: Ordering exists only inside a log

No ordering MUST be implied between different logs or different writers, even on the same channel. Consumers MUST NOT rely on cross-log sequence.

#### Scenario: Two writers' logs advance independently

- **WHEN** two writers publish simultaneously on the same channel
- **THEN** each log's sequences are contiguous independently, and no total order is claimed

### Requirement: A detected fork is kept as evidence

When a record contradicts stored history, the replica MUST keep both the stored record and the contradicting one, keyed by log and
sequence, instead of discarding the contradiction. Evidence MUST survive a restart when the store is durable.

#### Scenario: Both sides of a fork are retrievable

- **WHEN** a writer signs two different records for the same sequence and a replica sees both
- **THEN** the replica offers both records as evidence for that sequence, and the accepted chain is unchanged

#### Scenario: Evidence outlives a restart

- **WHEN** the node is restarted against a durable store after recording a fork
- **THEN** the evidence for that sequence is still retrievable

#### Scenario: Evidence is bounded

- **WHEN** a writer equivocates at thousands of sequences
- **THEN** the evidence a replica keeps is bounded by a configured limit, with the overflow counted rather than silently dropped

### Requirement: The application is told which record conflicted

A fork callback MUST receive the contradicting record, not just a notification that one occurred, so an application can log or
forward the evidence immediately.

#### Scenario: Callback carries the offending record

- **WHEN** an equivocating record is rejected
- **THEN** the callback receives that record's sequence, hash and payload

### Requirement: Ingest accepts a run of records with per-record outcomes

Ingest MUST accept a batch of records and MUST return one outcome per input record, in order. It MUST stop storing at the first
record that is not next for its log and MUST report the remaining records as not attempted rather than inventing outcomes. Batch
ingest MUST be equivalent to ingesting the same records one at a time: the same records are stored, the same outcomes are
reported, and the same counters move.

#### Scenario: A contiguous run is stored in one call

- **GIVEN** a follower that holds records 0..9 and a reply carrying 10..40
- **WHEN** the batch is ingested
- **THEN** all 31 are stored, each outcome is `Accepted`, and the store was called once for the log

#### Scenario: A gap truncates the batch

- **GIVEN** a reply carrying 10..40 where 15 is missing
- **WHEN** the batch is ingested
- **THEN** 10..14 are stored and accepted, 15..40 are reported as not attempted, and the batch stops

#### Scenario: A bad record inside a run is isolated

- **GIVEN** a reply where record 20 fails verification
- **WHEN** the batch is ingested
- **THEN** 10..19 are stored, 20 is reported as refused with its own outcome, and 21..40 are not attempted

### Requirement: Batch ingest never changes ordering guarantees

Application delivery MUST remain once per record and in sequence order, and a batch MUST NOT deliver records that individual
ingest would have withheld.

#### Scenario: Delivery matches per-record ingest

- **GIVEN** the same records ingested individually and as a batch
- **WHEN** the deliveries are compared
- **THEN** they are the same records in the same order
