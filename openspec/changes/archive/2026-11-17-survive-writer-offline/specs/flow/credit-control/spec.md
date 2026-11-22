# Spec Delta

## ADDED Requirements

### Requirement: The live path can be credit-paced

A receiver MAY grant credit to shape live delivery, and a sender MUST NOT exceed granted credit on that path. Without a grant the
live path stays best effort, so a deployment that does not care cannot be broken by the mechanism.

#### Scenario: Granted credit limits live delivery

- **WHEN** a receiver grants credit for ten records and the writer has more to send
- **THEN** no more than ten are delivered on the live path before more credit arrives

#### Scenario: No grant means best effort

- **WHEN** no credit has ever been granted on a link
- **THEN** live delivery behaves as before and nothing blocks

### Requirement: A follower too far behind stops receiving live records

A node that detects it is behind by more than a configured threshold MUST ignore live records for that log and catch up by
fetching, so its backlog is bounded by its own request size rather than by the writer's rate.

#### Scenario: A slow follower switches to repair

- **WHEN** a follower's gap for a log exceeds the threshold
- **THEN** it stops accepting live records for that log and converges by fetch

#### Scenario: Catching up restores live delivery

- **WHEN** the follower closes the gap
- **THEN** live delivery resumes for that log

#### Scenario: Memory stays bounded while the writer is faster

- **WHEN** a follower is slower than the writer for a sustained period
- **THEN** its unapplied backlog stays within the configured bound and it still converges when it can
