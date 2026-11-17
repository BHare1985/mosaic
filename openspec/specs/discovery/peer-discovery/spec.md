# discovery/peer-discovery Specification

## Purpose
Defines how a node learns which peers exist, so the topology is configuration rather than a coordination service.

## Requirements

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

### Requirement: Peers can be discovered without configuration

A node MUST be able to discover peers on its local network without any peer being named in configuration, and MUST be able to
start with discovery configured but no peer currently present. Discovery MUST be optional and MUST NOT change the behaviour of a
node configured with a static list only.

#### Scenario: A node with no configured peers finds one

- **WHEN** two nodes on the same network run with beacon discovery and empty peer lists
- **THEN** each learns the other's identity and endpoint and connects

#### Scenario: Static-only configuration is unaffected

- **WHEN** discovery is not configured
- **THEN** the node behaves exactly as before, connecting only to configured peers

#### Scenario: Starting alone is not an error

- **WHEN** a node starts with discovery enabled and no other node present
- **THEN** it runs, serves what it holds, and connects when a peer appears

### Requirement: Discovered peers are hints, never authority

A discovered peer MUST be treated exactly like a configured one for trust purposes: it MUST pass the same negotiation and key
policy before any record from it is accepted. Discovery MUST inform the transport who might be there, and MUST NOT decide who may
be trusted.

#### Scenario: An unwanted peer is discovered and refused

- **WHEN** a node in closed key mode discovers a peer whose key is not allowed
- **THEN** the connection is refused by the key policy, and the discovery layer does not retry it endlessly

#### Scenario: Discovery does not bypass negotiation

- **WHEN** a discovered peer speaks an unsupported protocol version
- **THEN** its payload is refused exactly as for a configured peer

### Requirement: Discovered state is bounded and expires

A node MUST bound the peers it tracks from discovery and MUST forget a peer that stops announcing, so a network cannot be turned
into unbounded memory or an ever-growing dial list by one host announcing identities.

#### Scenario: Peer churn does not accumulate

- **WHEN** peers appear and disappear repeatedly
- **THEN** the tracked set returns to the number of peers actually present

#### Scenario: A silent peer is dropped

- **WHEN** a peer stops announcing for longer than the configured timeout
- **THEN** it is removed from the peer set and not dialled again unless it reappears
