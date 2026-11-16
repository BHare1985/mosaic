# Spec Delta

## ADDED Requirements

### Requirement: Node behaviour is observable through counters

The node MUST expose cumulative counters for at least: records accepted, duplicates, gaps, forks, records rejected for a bad
signature or chain, repair requests sent, repair retries after timeout, and records dropped because credit was exhausted.
Counters MUST be values, not logging, so tests can assert on them.

#### Scenario: Ingest outcomes are countable

- **WHEN** a follower receives two valid records, one duplicate and one forged record
- **THEN** the counters report two accepted, one duplicate, one rejected, and the app saw two records

#### Scenario: Repair activity is countable

- **WHEN** a follower repairs a gap after a timeout
- **THEN** the counters report at least one request sent and one retry

#### Scenario: Credit exhaustion is visible

- **WHEN** a follower cannot request repair because credit is exhausted
- **THEN** the drop counter increases rather than the event being silent

## MODIFIED Requirements

### Requirement: Gaps are repaired from the sender or from any peer

When a record reveals a missing range, the node MUST request that range, MUST NOT duplicate an already outstanding request for the same range to the same peer, and MUST respect its credit and batch limits. After a timeout the request MUST be considered abandoned and retriable. Credit MUST be accounted per outstanding request, so a response that arrives late - after its request was abandoned - cannot increase a peer's available credit above the window capacity.

#### Scenario: Missing range is requested once

- **WHEN** several records reveal the same missing range before a response arrives
- **THEN** only one request for that range is outstanding per peer

#### Scenario: Request respects batch and credit limits

- **WHEN** a range larger than the configured batch or available credit is missing
- **THEN** the request covers at most that bound, and the remainder is requested afterwards

#### Scenario: A timed-out request is retried

- **WHEN** a repair response never arrives
- **THEN** the request is abandoned after the timeout and retried, and the follower still converges

#### Scenario: A late response does not create credit

- **WHEN** a repair response arrives after its request has been abandoned and the range has been re-requested
- **THEN** available credit to that peer never exceeds the window capacity

#### Scenario: Delayed responses do not inflate the window

- **WHEN** a link delays every response beyond the request timeout while losing nothing
- **THEN** the follower's outstanding requests to that peer stay within the window capacity and it still converges
