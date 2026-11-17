# Spec Delta

## ADDED Requirements

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
