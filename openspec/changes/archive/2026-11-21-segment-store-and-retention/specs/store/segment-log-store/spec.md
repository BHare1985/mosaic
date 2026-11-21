# Spec Delta

## Purpose

Defines a durable store whose layout is the log's own shape: append-only segment files per log, an index, and recovery that
refuses to invent history after a crash.

## ADDED Requirements

### Requirement: A log is a directory of append-only segments

The store MUST write each log to its own directory, addressed by `(writer identity, channel)`, containing fixed-size segment
files whose names give their first sequence number. Records MUST be appended in sequence order, and a segment MUST never be
rewritten once it is full.

#### Scenario: Segments roll over

- **GIVEN** a segment size limit and enough records to exceed it
- **WHEN** the writer appends
- **THEN** a new segment begins at the next sequence, and the previous segment is not modified

#### Scenario: Enumeration finds every log

- **GIVEN** a store with several logs across several segments
- **WHEN** the store is enumerated
- **THEN** every log is listed once, with its head sequence

### Requirement: Opening a store recovers a torn tail and refuses invented history

On open, the store MUST truncate an incomplete final record, MUST NOT modify any complete record, and MUST report the recovered
head. If a segment disagrees with the index about a record's position, the store MUST treat the record as missing for reads past
that point rather than returning bytes it cannot vouch for.

#### Scenario: A crash mid-append

- **GIVEN** a store where the last append was interrupted halfway through a record
- **WHEN** the store is reopened
- **THEN** the partial record is discarded, the head is the last complete record, and appending continues at head+1

#### Scenario: A damaged segment does not invent data

- **GIVEN** a segment whose index entry points past the end of the file
- **WHEN** a read is requested for that sequence
- **THEN** the read reports the record missing and does not return adjacent bytes

### Requirement: The format is documented and versioned

The segment and index layout MUST be documented with a format byte in the store header, and a store written by a newer format
MUST be refused with a clear error rather than parsed optimistically.

#### Scenario: An unknown format is refused

- **GIVEN** a store directory whose header declares an unsupported format version
- **WHEN** the store is opened
- **THEN** opening fails with an explicit error and no file is modified

#### Scenario: The format is testable from the outside

- **GIVEN** the documented layout and a store written by the test suite
- **WHEN** the bytes are inspected by a reader that knows only the documentation
- **THEN** the records can be recovered, because the documentation is sufficient
