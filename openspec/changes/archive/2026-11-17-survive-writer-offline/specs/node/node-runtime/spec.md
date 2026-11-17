# Spec Delta

## ADDED Requirements

### Requirement: Newcomers can learn heads from any peer

A node MUST be able to ask its peers for the heads of the logs matching a set of patterns, and MUST answer such a request from
its own store. This is what lets a log be found and repaired while its writer is offline.

#### Scenario: A newcomer finds a log with the writer away

- **WHEN** a node joins and the log's writer is not reachable, but a replica holds the log
- **THEN** the newcomer learns the head from that replica and repairs up to it

#### Scenario: A replica answers with what it actually holds

- **WHEN** a peer asks for heads matching a pattern
- **THEN** the node replies only for logs it holds, with the sequence and hash it has stored

### Requirement: Retention state is observable

A node MUST expose how many records it is retaining for replication, and for which logs it is below its `min_replicas` floor,
so an operator can see that durability is unmet instead of inferring it.

#### Scenario: Unmet replication is visible

- **WHEN** a writer is holding records because too few replicas acknowledged
- **THEN** a counter and the affected logs are readable without parsing logs
