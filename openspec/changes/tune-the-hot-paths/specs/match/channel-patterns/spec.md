# Spec Delta

## Purpose

Defines what a pattern means. This delta adds a cost property to matching and states that making it cheaper may not change what
it decides.

## MODIFIED Requirements

### Requirement: Match cost is independent of non-matching subscriptions

A match MUST cost time proportional to the subscriptions that share a channel prefix, not to the whole subscription set. The
implementation MUST NOT allocate or copy shared state per lookup: scratch memory is reused across lookups, so a steady-state
match performs no allocation at all. The result MUST be identical to the straightforward interpretation of the patterns, for every
pattern the matcher accepts.

#### Scenario: Randomised agreement with the naive definition

- **WHEN** a large random set of patterns and channels is compared against a naive matcher
- **THEN** the two agree on every channel, including after removals

#### Scenario: A steady-state match allocates nothing

- **GIVEN** a matcher with subscriptions loaded and one warm-up match
- **WHEN** a further match runs
- **THEN** no heap allocation occurs during the match

#### Scenario: Optimisation does not change the answer

- **GIVEN** a generated set of patterns and channels, including wildcards, deep nesting and near-misses
- **WHEN** matching runs through the optimised path and through a reference implementation
- **THEN** the two agree on every pair

#### Scenario: Cost stays linear as subscriptions grow

- **GIVEN** the benchmark's subscription counts of 1k, 10k and 100k
- **WHEN** matching a fixed channel is measured
- **THEN** the published figures show no worse than linear growth, and the change records both the before and after numbers
