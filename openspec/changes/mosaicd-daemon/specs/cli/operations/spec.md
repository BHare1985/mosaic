# Spec Delta

## ADDED Requirements

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
