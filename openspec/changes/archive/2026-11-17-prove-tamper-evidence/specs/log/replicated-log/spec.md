# Spec Delta

## ADDED Requirements

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
