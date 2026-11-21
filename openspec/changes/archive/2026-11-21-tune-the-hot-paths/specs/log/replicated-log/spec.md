# Spec Delta

## Purpose

Defines how a replica turns a stream of records into stored history. This delta adds batch ingest, because repair delivers runs
of records and one store round trip per record is the dominant cost of convergence.

## ADDED Requirements

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
