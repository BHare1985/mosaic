# Spec Delta

## Purpose

Defines how a node learns addresses. This delta adds peer-list gossip for networks where the beacon cannot reach, under the same
rule that already governs discovery: learning an address grants nothing.

## ADDED Requirements

### Requirement: A gossiped identity is bound to an address, not to a key

An entry MUST be treated as a claim about where an identity can be reached, never as proof of which key that identity holds. A
node MUST NOT accept a record, or grant a write right, because of a gossiped binding; and a peer that misdirects a dial MUST cost
only the dial, because the pin policy still decides whether the key behind an address is the one expected.

#### Scenario: A misdirected dial cannot forge history

- **GIVEN** a hostile peer that gossips an address for a victim identity where no such peer lives
- **WHEN** a node dials that address
- **THEN** the link either does not establish or establishes with a different key, and no record is believed on the strength of
  the identity the entry claimed

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

### Requirement: Freshness only improves, and live links are not evictable

An entry MUST NOT be refreshed by a copy describing an older state than the one already held, so a peer cannot keep a dead node
in the table by replaying a stale entry forever. A node MAY only evict an entry whose peer it is not currently connected to, so a
flood of legal-looking entries cannot push a live link out of the table.

#### Scenario: A stale copy does not refresh

- **GIVEN** an entry held for 200 ms
- **WHEN** a peer sends the same entry describing it as 400 ms old
- **THEN** the entry is not refreshed, the refusal is counted, and the node keeps what it had

#### Scenario: A connected peer survives a flood

- **GIVEN** a table at its bound with one entry for a peer the transport is talking to
- **WHEN** a flooding peer adds more valid entries than the bound allows
- **THEN** the connected peer's entry survives, the excess is counted, and the table stays within its bound

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

#### Scenario: A window that rotates rather than always the freshest

- **GIVEN** more entries in the table than one message may carry
- **WHEN** a node gossips over several rounds
- **THEN** the window of entries it sends moves, so slower-refreshing entries eventually reach a third hop instead of starving

#### Scenario: A message from the future is refused

- **GIVEN** a peer-list entry whose advertised age is negative or beyond the limit
- **WHEN** it is handled
- **THEN** the entry is refused and counted, and no clock arithmetic is trusted
