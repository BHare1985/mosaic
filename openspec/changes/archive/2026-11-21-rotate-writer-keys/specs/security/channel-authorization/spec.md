# Spec Delta

## Purpose

Defines who may write a channel. This delta lets a rule name a rotation chain, so that a writer can rotate its key without every
peer needing a configuration change at the same moment.

## MODIFIED Requirements

### Requirement: Channels may name the writers allowed to write them

A node MUST accept per-channel-pattern rules naming permitted writer identities, and MUST accept a rule that names a rotation
root instead: such a rule authorizes that identity and the bounded rotation chain it signs in its own log. A record MUST be
accepted only when its writer is named directly or reachable from a named root, and MUST NOT be authorized because a record
claims to be a successor - the claim is worth only the root's signature. Channels without a rule MUST remain open, so a
deployment that needs no authorization is unaffected.

#### Scenario: An unlisted writer is refused

- **WHEN** a rule permits writer A on `audit.>` and a record arrives from writer B
- **THEN** the record is rejected, never stored, and the rejection is counted

#### Scenario: Rules are matched by pattern

- **WHEN** a rule names `svc.resize` and records arrive on `svc.resize` and `svc.render`
- **THEN** only the first is subject to the rule

#### Scenario: An open channel stays open

- **WHEN** no rule matches a channel
- **THEN** any verified writer may write it, as before

#### Scenario: A rotated key stays authorized

- **GIVEN** a rule naming writer A as a rotation root, and A's log containing a valid rotation link to B
- **WHEN** B publishes on that channel
- **THEN** the record is accepted, without a configuration change on any peer

#### Scenario: A rotation link is not a grant

- **GIVEN** a rule that allows only B, and a rotation link from A to B
- **WHEN** A publishes on that channel
- **THEN** the record is refused: a chain authorizes the successor, never the predecessor

