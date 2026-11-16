# Spec Delta

## ADDED Requirements

### Requirement: A node can run as a pure replica

A node MUST be usable with no application callback at all, following patterns and holding logs for others while serving repair
requests. This makes a store-only process and a tail-only process meaningful without inventing a separate component.

#### Scenario: A replica holds logs it never delivers

- **WHEN** a node follows a broad pattern with no application callback configured
- **THEN** it stores every matching log, answers repair requests for those logs, and never invokes a callback

#### Scenario: A second follower repairs from the pure replica

- **WHEN** a follower starts after the writer has gone offline and requests records from the pure replica
- **THEN** the follower converges from the replica without contacting the writer
