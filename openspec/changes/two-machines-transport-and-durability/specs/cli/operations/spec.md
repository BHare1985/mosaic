# Spec Delta

## Purpose

Defines the operator-facing surface that makes the library usable without writing code: publishing to a channel and tailing channel patterns.

## ADDED Requirements

### Requirement: A user can publish records to a channel

A command MUST publish records read from standard input or a file to a named channel on a node identified by its own key, and
MUST report the resulting sequence and hash for each published record. Invalid channels MUST be rejected before publishing.

#### Scenario: Publishing reports position in the log

- **WHEN** three records are published on events.a
- **THEN** the command reports sequences 0, 1 and 2 with their hashes

#### Scenario: An invalid channel is refused

- **WHEN** a channel containing a wildcard segment is supplied
- **THEN** the command fails with a message and publishes nothing

### Requirement: A user can tail channel patterns from a remote node

A command MUST subscribe to one or more patterns on another node and stream matching records to standard output as they arrive,
including records recovered by repair, without duplicating a record it has already printed.

#### Scenario: Tailing a pattern on another host

- **WHEN** a user tails events.> on host B while records are published on host A
- **THEN** each matching record is printed exactly once, in sequence order per log

#### Scenario: A late-joining tail prints history first

- **WHEN** a tail starts after records already exist
- **THEN** it prints the recovered records before the live ones, in sequence order

### Requirement: Streaming output stops cleanly

A tail MUST stop on interrupt and exit successfully, and a publish MUST exit non-zero when a record could not be published.
Output MUST be usable from a pipeline: one record per line, no interactive prompts, no colour on a non-terminal.

#### Scenario: Interrupt exits successfully

- **WHEN** a running tail is interrupted
- **THEN** it exits with a success status and prints nothing further

#### Scenario: Failed publish is a non-zero exit

- **WHEN** a record cannot be published
- **THEN** the command exits non-zero and the failure is reported on standard error
