# Spec Delta

## ADDED Requirements

### Requirement: Following is reversible and publishing can batch

A node MUST be able to stop following a pattern, after which no further records for it are delivered or stored, and MUST be able
to publish several records in one call with the same ordering and signing guarantees as individual publishes.

#### Scenario: Unfollow stops delivery

- **WHEN** a node stops following a pattern
- **THEN** later records on matching channels are neither stored nor delivered

#### Scenario: A batch publishes in order

- **WHEN** several payloads are published in one call
- **THEN** they occupy consecutive sequences in the log, in the order given
