# proto/peer-advertisement Specification

## Purpose
Defines the self-description peers exchange: which channels they follow, and how much spare work they have. It is the
mechanism that lets a node choose a peer to ask without a coordinator, and it is deliberately weak - every use of it MUST be
safe when the advertisement is wrong, stale or hostile.

## Requirements

### Requirement: A peer advertises what it follows

A node MUST advertise its current subscription patterns in its hello, deduplicated and bounded by the configured head-pattern
limit. A receiver MUST record them per peer and MUST replace an earlier set when a newer hello arrives. A hello with more
patterns than the limit MUST be truncated to the limit and the truncation counted, never rejected outright.

#### Scenario: Patterns replace, not accumulate

- **GIVEN** a peer whose first hello advertises `events.>` and whose second advertises `storage.>`
- **WHEN** the second hello is handled
- **THEN** the peer's follow set is `storage.>` alone

#### Scenario: An oversized advertisement is bounded

- **GIVEN** a hello advertising more patterns than the limit
- **WHEN** it is handled
- **THEN** at most the limit is recorded, and the truncation is visible in the counters

### Requirement: Advertisements are hints, never authority

A node MUST NOT deliver a record to a peer, accept a record from a peer, or grant a channel write right on the strength of an
advertised pattern. Advertisements MUST be used only to choose among peers that are already acceptable for a different reason
(configured, discovered and pinned, or authorized by a rule). Sales pitch: a peer that advertises a pattern it does not follow
loses messages; one that hides a pattern it follows loses messages too. Neither gains anything.

#### Scenario: A peer advertising a channel it may not write

- **GIVEN** a channel closed to a peer by a channel rule, and that peer advertising a following pattern for it
- **WHEN** the peer publishes on that channel
- **THEN** the record is refused by the rule, and the advertisement has no effect

#### Scenario: A silent advertisement cannot break delivery

- **GIVEN** a peer that advertises no patterns
- **WHEN** it is configured as a follower and the writer publishes
- **THEN** live delivery and repair still reach it, because delivery is decided by matching on the sender's side, not by the
  advertisement

### Requirement: A peer advertises spare capacity

A node MUST advertise a coarse spare-capacity figure with each hello, derived from its own pending work, and MUST treat a
received figure as a reason to prefer or skip a peer rather than as a measured fact. A figure of zero MUST mean "prefer someone
else", not "unreachable": if no peer advertises spare capacity, a node MUST still be able to send work to the best-ranked peer
available.

#### Scenario: A saturated worker is skipped

- **GIVEN** two workers following `svc.compress`, one advertising spare capacity and one advertising none
- **WHEN** a job is sent
- **THEN** the ranking prefers the one with spare capacity

#### Scenario: Everyone saturated still makes progress

- **GIVEN** every worker advertises no spare capacity
- **WHEN** a job is sent
- **THEN** it is sent to the top-ranked worker anyway, and the request is not lost

#### Scenario: Stale capacity expires

- **GIVEN** a worker that advertised spare capacity and then stopped sending hellos
- **WHEN** the hello interval passes several times
- **THEN** its advertised capacity is treated as unknown, not as spare
