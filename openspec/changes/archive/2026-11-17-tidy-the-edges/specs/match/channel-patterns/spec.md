# Spec Delta

## ADDED Requirements

### Requirement: Subscriptions can be listed and removal releases storage

A subscriber MUST be able to enumerate its active patterns, and removing a subscription MUST release the storage its patterns held,
so a node that adds and removes subscriptions repeatedly returns to its baseline footprint.

#### Scenario: Patterns are listable

- **WHEN** three subscriptions are active and one is removed
- **THEN** listing returns the two that remain

#### Scenario: Churn does not accumulate

- **WHEN** a subscription is added and removed many times
- **THEN** the matcher's reported size returns to its starting value
