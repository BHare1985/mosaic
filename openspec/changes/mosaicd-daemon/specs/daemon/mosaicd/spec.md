# Spec Delta

## Purpose

Defines how Mosaic runs as a service: what an operator writes in a config file, what the daemon refuses to start without, how it
stops, and what it reports while it runs - so deployment is a file and a process rather than a C++ caller.

## ADDED Requirements

### Requirement: Every operational setting can be set from a config file

A configuration file MUST be able to set the node's identity, store location, listen address, peer list, followed patterns, and
every limit and interval the node supports. Every setting MUST have a documented default, and a setting that is absent MUST take
that default rather than failing.

#### Scenario: A minimal config starts a working node

- **WHEN** a config file names only an identity, a store and a peer
- **THEN** the daemon starts with documented defaults for everything else and replicates what it follows

#### Scenario: Every limit is expressible

- **WHEN** an operator sets a fetch window, a `min_replicas` floor and a record size limit in the file
- **THEN** those values are what the running node uses, not the compiled defaults

### Requirement: Configuration is validated before anything starts

A daemon MUST validate the whole file before opening a socket or a store, MUST report every error it finds (not only the first),
and MUST refuse to start. An unknown key MUST be an error: a typo MUST NOT silently become a default.

#### Scenario: A typo stops the daemon

- **WHEN** the file contains a key the daemon does not know
- **THEN** startup fails, the offending key is named, and no socket or store is opened

#### Scenario: All problems are reported at once

- **WHEN** three settings are invalid
- **THEN** all three are listed in one failure, so an operator does not fix them one restart at a time

#### Scenario: Values are checked, not coerced

- **WHEN** a numeric setting contains text, or a required setting is missing
- **THEN** startup fails with a message naming the setting and what was expected

### Requirement: The defaults are printable and authoritative

A daemon MUST be able to print a complete, commented configuration showing every setting and its default, generated from the same
values the code uses, so the documented defaults cannot drift from the running ones.

#### Scenario: The printed config runs as-is

- **WHEN** the daemon is asked for its defaults and that output is used as a config file (with identity, store and peers filled
  in)
- **THEN** it starts successfully and reports the same values it printed

### Requirement: The daemon stops cleanly when asked

On `SIGINT` or `SIGTERM` the daemon MUST finish the step it is in, close its store so buffered state is committed, and exit with
a success status. It MUST NOT abandon a partially written record.

#### Scenario: Interrupt exits cleanly and durably

- **WHEN** a running daemon receives an interrupt
- **THEN** it exits 0, and everything it had accepted before the interrupt is present on the next start

#### Scenario: A config change is a restart, not a reload

- **WHEN** the config file is edited while the daemon runs
- **THEN** the running daemon keeps its loaded configuration, and the documented way to apply a change is to restart

### Requirement: What the daemon is doing is observable

A daemon MUST report counters periodically at its configured log level - records accepted, duplicates, gaps, forks, repair
requests, retention shortfall, throttled requests, rejected handshakes - and MUST be able to dump them on demand without
stopping the process.

#### Scenario: A stuck deployment is diagnosable

- **WHEN** a daemon is not converging
- **THEN** its counters show whether it is receiving nothing, rejecting handshakes, being throttled, or repairing repeatedly

#### Scenario: Log level controls volume, not content

- **WHEN** the log level is raised
- **THEN** more detail is printed about the same events, and no category of event is hidden at the default level
