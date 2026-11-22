# Spec Delta

## ADDED Requirements

### Requirement: The transport exposes the peers it can address

A transport MUST expose a best-effort snapshot of the peers the node can currently address, excluding itself. The set MAY be
empty before discovery and MAY change between calls; it MUST NOT be treated as a membership agreement or a promise that a peer
is reachable.

#### Scenario: Peers are listed without self

- **WHEN** two nodes are connected and either asks for its peers
- **THEN** the other node is listed and the caller is not

#### Scenario: An empty view is allowed

- **WHEN** no peer has been discovered yet
- **THEN** the snapshot is empty and the caller keeps working
