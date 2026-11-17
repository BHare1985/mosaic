# Spec Delta

## ADDED Requirements

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
