# security/channel-authorization Specification

## Purpose
Defines which writer identities are allowed to write which channels, so that a shared log can carry a rule about who owns a
topic - and so that a job request cannot be injected by any node that happens to hold a key.

## Requirements

### Requirement: Channels may name the writers allowed to write them

A node MUST accept per-channel-pattern rules naming permitted writer identities. A record whose writer is not permitted MUST NOT
be stored, delivered or acknowledged as accepted history. Channels without a rule MUST remain open, so a deployment that needs no
authorization is unaffected.

#### Scenario: An unlisted writer is refused

- **WHEN** a rule permits writer A on `audit.>` and a record arrives from writer B
- **THEN** the record is rejected, never stored, and the rejection is counted

#### Scenario: Rules are matched by pattern

- **WHEN** a rule names `svc.resize` and records arrive on `svc.resize` and `svc.render`
- **THEN** only the first is subject to the rule

#### Scenario: An open channel stays open

- **WHEN** no rule matches a channel
- **THEN** any verified writer may write it, as before

### Requirement: Authorization is enforced on both sides of a write

A writer MUST refuse to publish a record its own rules would not accept, so the log it owns never contains history that its
readers will reject.

#### Scenario: A writer will not publish into a channel it is not allowed to write

- **WHEN** a node with a rule for `svc.resize` tries to publish there as an unlisted identity
- **THEN** the publish is refused and nothing is appended

### Requirement: Rejected writes are visible

A node MUST count rejections per channel pattern, so an operator can see that a rule is doing something rather than assume it.

#### Scenario: Rejections are countable

- **WHEN** three unauthorised records are refused
- **THEN** the rejection counter increases by three and the accepted log is unchanged
