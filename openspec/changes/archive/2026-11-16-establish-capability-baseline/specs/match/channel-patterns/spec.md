# Spec Delta

## Purpose

Defines channel names, the subscription pattern language, and the matching decision that determines which channels a subscriber receives.

## ADDED Requirements

### Requirement: Channels are dotted names with reserved wildcard segments

A channel MUST be a non-empty dotted name such as storage.eu.shard7, with non-empty segments. The segments * and > MUST be reserved for patterns and MUST NOT appear in a published channel.

#### Scenario: A published channel uses literal segments

- **WHEN** a writer publishes to storage.eu.shard7
- **THEN** the channel is accepted as a literal name

#### Scenario: Wildcard segment in a channel is rejected

- **WHEN** a publisher attempts to publish to a channel containing * or >
- **THEN** the channel is rejected as invalid

### Requirement: Patterns select one or many segments

A subscription pattern MUST be a literal name, or use * for exactly one segment and > for one or more trailing segments. > MUST appear only in the final position. Any other use of wildcards MUST be rejected rather than silently treated as literal text.

#### Scenario: Star matches exactly one segment

- **WHEN** the pattern storage.*.shard7 is matched against storage.eu.shard7
- **THEN** the channel matches

#### Scenario: Star does not match a different depth

- **WHEN** the pattern storage.*.shard7 is matched against storage.eu.frankfurt.shard7
- **THEN** the channel does not match

#### Scenario: Tail matches one or more segments

- **WHEN** the pattern storage.> is matched against storage.eu and against storage.eu.shard7
- **THEN** both channels match

#### Scenario: Malformed patterns are rejected

- **WHEN** a pattern places > anywhere but last, or contains an empty segment
- **THEN** the pattern is rejected at subscription time

### Requirement: Matching is decided on the channel name only

In this baseline version a subscription MUST match on the channel name alone. Record content type, payload and headers MUST NOT influence matching. Adding field-level matching requires a change to this requirement.

#### Scenario: Payload does not affect matching

- **WHEN** two records on a matching channel carry different content types and payloads
- **THEN** both are delivered to the same subscriber

### Requirement: A subscriber receives a channel only when a pattern matches

Delivery MUST be governed by the subscriber's pattern set: a channel that matches no pattern of a subscriber MUST NOT be delivered to it. A transport MAY over-deliver, so the node MUST re-check the pattern set before storing or notifying.

#### Scenario: Non-matching channel is withheld

- **WHEN** a node follows events.a and a record arrives on events.b
- **THEN** the record is not stored and not delivered

#### Scenario: Transport over-delivery is filtered

- **WHEN** a transport hands a node a record whose channel matches none of its patterns
- **THEN** the node discards it without notifying the application

### Requirement: Subscriptions can be added and removed without interference

Adding or removing a subscription MUST NOT change the matching result of any other subscription. Reusing a slot after removal MUST NOT deliver to the removed subscription.

#### Scenario: Removal stops delivery

- **WHEN** a subscription matching events.a is removed
- **THEN** subsequent records on events.a are no longer delivered to it

#### Scenario: Churn restores baseline state

- **WHEN** many subscriptions are added and then all removed
- **THEN** a fresh subscription behaves exactly as on a fresh matcher, with no stale matches

### Requirement: Match cost is independent of non-matching subscriptions

The matching decision MUST NOT cost time proportional to the number of subscriptions that do not match, so that one subscriber's patterns cannot slow down delivery for others. Results MUST agree with the plain segment-by-segment definition.

#### Scenario: Randomised agreement with the naive definition

- **WHEN** a large random set of patterns and channels is compared against a naive matcher
- **THEN** the two agree on every channel, including after removals
