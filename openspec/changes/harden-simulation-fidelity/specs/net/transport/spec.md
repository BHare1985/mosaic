# Spec Delta

## ADDED Requirements

### Requirement: The conformance suite is reusable by any transport implementation

The transport conformance suite MUST be expressed against the transport contract and a factory that produces an
implementation, so a new transport is verified by supplying itself and no other change. The suite MUST cover best-effort
delivery, absence of self-delivery, publication filtering, silent drop of unicast to unknown peers, stable identity and
non-blocking poll.

#### Scenario: An implementation is added by supplying a factory

- **WHEN** a transport implementation is introduced
- **THEN** running the existing suite against it requires no edit to the suite

#### Scenario: A violating transport fails the suite

- **WHEN** an implementation delivers a node's own publication back to it
- **THEN** the suite fails on the self-delivery rule
