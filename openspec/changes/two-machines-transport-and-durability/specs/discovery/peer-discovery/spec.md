# Spec Delta

## Purpose

Defines how a node learns which peers exist, so the topology is configuration rather than a coordination service.

## ADDED Requirements

### Requirement: Peers may be supplied statically

A node MUST accept a configured list of peer identities and endpoints and connect to them, so a deployment can run with no
discovery mechanism at all. Configuration MUST be per node and MUST NOT require a central server.

#### Scenario: Configured peers connect

- **WHEN** two nodes are configured with each other's identity and endpoint
- **THEN** they connect and exchange traffic without any other configuration

#### Scenario: No discovery service is required

- **WHEN** the network is fully described by static configuration
- **THEN** replication works with no rendezvous or registry service running

### Requirement: Losing one peer does not stop the rest

Failure of any single peer MUST NOT prevent remaining peers from exchanging traffic. A node MUST continue operating with the
peers it has rather than treating a missing peer as fatal.

#### Scenario: A dead peer is not fatal

- **WHEN** one of three configured peers is permanently absent
- **THEN** the other two keep replicating and the node keeps serving requests

#### Scenario: Unreachable peers do not block startup

- **WHEN** a node starts while a configured peer is down
- **THEN** it starts, serves what it holds, and connects when that peer returns

### Requirement: Discovery never becomes consensus

Peer discovery MUST NOT be required to agree on log content, ordering or membership. A node with a partial peer view MUST still
be able to replicate and repair from the peers it can reach, because logs are self-verifying.

#### Scenario: Partial view still converges

- **WHEN** a follower can reach only one of several peers holding the same log
- **THEN** it converges from that one peer
