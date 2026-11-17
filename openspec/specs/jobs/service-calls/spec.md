# jobs/service-calls Specification

## Purpose
Defines how a service is called by name over logs: a request record on the service channel, deterministic worker ranking, and a
result record the requester can read back after a restart.

## Requirements

### Requirement: A job request and result are records

A job request MUST be an ordinary record on a service channel and a result an ordinary record on the requester's channel, both
carrying a compact binary envelope. Because they are records, they MUST inherit signing, per-log ordering, durability,
replication and replay without jobs knowing anything about those layers.

#### Scenario: A request survives the requester restarting

- **WHEN** a requester publishes a request and is restarted before the result arrives
- **THEN** it finds the request in its own log and can still accept the result

#### Scenario: Envelopes round-trip exactly

- **WHEN** a request or result is encoded and decoded
- **THEN** every field is preserved, and an envelope that cannot be decoded is rejected rather than half-read

### Requirement: Exactly one worker runs a job in the common case

Workers MUST rank themselves for a job id with rendezvous ranking over the worker set they know, and the highest-ranked worker
MUST run the job. Ranking MUST be deterministic from the job id and the worker set alone, so no coordination is needed.

#### Scenario: All healthy workers pick the same runner

- **WHEN** three workers see the same request and the same worker set
- **THEN** exactly one runs it and the others do not

#### Scenario: Removing a worker only reassigns its own jobs

- **WHEN** a worker leaves the set
- **THEN** jobs it did not rank first for are unaffected

### Requirement: A job is not lost when its first worker dies

A worker that is not ranked first MUST run the job after waiting its rank's share of the request timeout, unless a result for
that job id has already appeared on the reply channel. Delivery is at-least-once: a slow first worker and a later one may both
run, so handlers MUST be idempotent by job id and the requester MUST accept only the first result per job id.

#### Scenario: The first-ranked worker never answers

- **WHEN** rank 0 receives the request but dies before publishing a result
- **THEN** rank 1 runs the job after its wait and publishes the result

#### Scenario: A duplicate run is absorbed

- **WHEN** two workers both run the same job id
- **THEN** the requester accepts the first result and ignores the second

#### Scenario: A result already published stops the next rank

- **WHEN** a worker's turn comes and a result for that job id is already on the reply channel
- **THEN** it does not run the job

### Requirement: A requester learns that nothing happened

A requester MUST report a timeout when no result arrives within its wait, rather than blocking forever or reporting success.

#### Scenario: No worker is available

- **WHEN** no worker runs the job within the configured wait
- **THEN** the requester reports a timeout that names the job id

#### Scenario: Capacity is not silently ignored

- **WHEN** an operator runs a job with workers that advertise no capacity
- **THEN** the timeout is what they observe, and the current worker set is reported with it
