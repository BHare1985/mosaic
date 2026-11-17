# cli/operations Specification

## Purpose
Defines the operator-facing surface that makes the library usable without writing code: publishing to a channel and tailing channel patterns.

## Requirements

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

### Requirement: A store can be verified end to end

A command MUST walk every log in a store, re-verify every signature and hash link, and report either success with a count of
records verified or the first failure naming the log, sequence and reason. It MUST exit non-zero on any failure.

#### Scenario: An intact store verifies

- **WHEN** the command runs against a store written by a healthy node
- **THEN** it reports the number of records and logs verified and exits successfully

#### Scenario: A tampered record is caught

- **WHEN** one stored record's payload is altered
- **THEN** the command reports the log and sequence where the chain breaks, and exits non-zero

#### Scenario: A truncated log is reported, not ignored

- **WHEN** a log's stored records have a gap
- **THEN** the command names the missing range rather than reporting success

### Requirement: Commands accept a config file, with flags taking precedence

The existing commands MUST accept a config file that supplies the same settings the daemon reads, and an explicit command-line
flag MUST override the file. Settings that only the daemon needs MUST be ignored rather than rejected when a command reads the
same file.

#### Scenario: One file drives both

- **WHEN** a config file names the identity, store and peers, and the command is run with only the file and a channel
- **THEN** the command uses those values

#### Scenario: A flag wins over the file

- **WHEN** the file names one store and the command also passes a store flag
- **THEN** the flagged store is used
