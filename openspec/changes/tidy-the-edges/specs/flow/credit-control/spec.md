# Spec Delta

## ADDED Requirements

### Requirement: Credit invariants hold under arbitrary sequences

Randomised sequences of spends, grants and resets MUST NOT be able to take available credit below zero or above capacity, and a
grant MUST NOT reduce it. These MUST be checked by property tests, not only by examples.

#### Scenario: Randomised operations preserve the invariant

- **WHEN** a thousand random spend/grant/reset operations are applied
- **THEN** available credit is in `[0, capacity]` after every one

#### Scenario: Grants are monotone

- **WHEN** any grant is applied
- **THEN** available credit does not decrease
