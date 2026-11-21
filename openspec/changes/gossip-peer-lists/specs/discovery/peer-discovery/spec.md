# Spec Delta

## Purpose

Defines how a node learns addresses. This delta adds peer-list gossip for networks where the beacon cannot reach, under the same
rule that already governs discovery: learning an address grants nothing.

## ADDED Requirements

### Requirement: Peers may be learned from other peers

A node MUST be able to accept peer-list entries from peers, and MUST treat them exactly as beacon discoveries: as candidate
addresses to be verified by the CURVE pin policy before use, never as authorization. A gossiped entry MUST NOT override a
configured peer's endpoints for the same identity, and MUST NOT lower the trust of an identity already known.

#### Scenario: A gossiped peer is dialled and pinned

- **GIVEN** a node that learns a peer address through gossip
- **WHEN** it dials that peer
- **THEN** the usual handshake and pin policy apply, and an unlisted key is refused exactly as it would be for a configured peer

#### Scenario: Gossip cannot rewrite configuration

- **GIVEN** a configured peer at address A and a gossip entry claiming the same identity at address B
- **WHEN** the entry is handled
- **THEN** the node keeps dialling A, and B is ignored for that identity

#### Scenario: Losing the introducer does not lose the peer

- **GIVEN** three nodes where C learned of B only through A
- **WHEN** A is stopped
- **THEN** C keeps its link to B and continues to replicate, and B's address survives A's departure

### Requirement: Gossip is bounded in size, hops and lifetime

A peer list MUST be bounded per message and per table, MUST carry an age per entry, MUST apply a hop limit, and MUST expire entries
that are not refreshed. A node MUST stop propagating an entry once it has aged out, and MUST count entries refused for being
oversized or malformed.

#### Scenario: A flood does not grow the table without bound

- **GIVEN** a peer that gossips more entries than the per-message cap, repeatedly
- **WHEN** the entries are handled
- **THEN** the table stays within its bound, the excess is counted, and the peer earns a strike rather than memory

#### Scenario: Stale entries expire

- **GIVEN** a gossiped entry that is never refreshed
- **WHEN** its age limit passes
- **THEN** it is removed from the table, and the node does not dial it again until it is re-learned

#### Scenario: A message from the future is refused

- **GIVEN** a peer-list entry whose advertised age is negative or beyond the limit
- **WHEN** it is handled
- **THEN** the entry is refused and counted, and no clock arithmetic is trusted
