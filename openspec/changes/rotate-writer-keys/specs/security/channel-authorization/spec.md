# Spec Delta

## Purpose

Defines who may write a channel. This delta lets a rule name a rotation chain, so that a writer can rotate its key without every
peer needing a configuration change at the same moment.

## MODIFIED Requirements

### Requirement: A channel rule names the writers allowed on it

A rule MAY name a writer identity directly, and MAY name a rotation root that authorizes that writer and its bounded rotation
chain. A record MUST be accepted only when its writer identity is either named directly or reachable from a named root by a valid,
bounded rotation chain. A rule MUST NOT authorize an identity because a record claims to be its successor; the claim is only
worth what the root's signature makes it worth.

#### Scenario: A rotated key stays authorized

- **GIVEN** a rule allowing writer A, and A's log containing a valid rotation link to B
- **WHEN** B publishes on the channel
- **THEN** the record is accepted

#### Scenario: An unlinked key is refused

- **GIVEN** a rule allowing writer A and an unrelated writer C
- **WHEN** C publishes on the channel
- **THEN** the record is refused, and the refusal is counted

#### Scenario: A rotation link is not a grant

- **GIVEN** a rotation link from A to B where the rule allows only B
- **WHEN** A publishes on the channel
- **THEN** the record is refused: a chain authorizes the successor, never the predecessor
