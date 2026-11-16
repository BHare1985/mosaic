# Spec Delta

## Purpose

Provides a deterministic, fault-injecting network and scenario runner, so that system-level requirements about convergence and repair are proven against adversarial conditions rather than a single benign run.

## ADDED Requirements

### Requirement: Simulation is fully determined by its seed

Every random choice a simulation makes - whether a message is lost, whether it is duplicated, when it is delivered and in
which order relative to other messages - MUST derive from one seed and the scenario's own actions. No wall clock, thread or
environment input may influence a run, so a failure is reproducible from the seed and the scenario alone.

#### Scenario: Same seed, same outcome

- **WHEN** a scenario is run twice with the same seed
- **THEN** every delivery, drop, duplication and delivery order is identical

#### Scenario: A reported seed reproduces the failure

- **WHEN** a sweep reports a failing seed
- **THEN** re-running that scenario with only that seed reproduces the same failure

### Requirement: Faults are injectable per link and per direction

The harness MUST support, independently per link and per direction: loss, duplication, delay, reordering, and partition with
later healing. Faults MUST be configurable while a scenario runs, so a scenario can break and then heal the network.

#### Scenario: Delay is applied to delivery, not to the sender

- **WHEN** a link is configured with a delay
- **THEN** the sender returns immediately and the message becomes available to the receiver only after the simulated delay

#### Scenario: Reordering actually reorders

- **WHEN** a link is configured to reorder
- **THEN** at least one pair of messages is delivered in the opposite order to which it was sent

#### Scenario: A partition breaks then heals

- **WHEN** a partition is applied and later removed
- **THEN** no message crosses the partition while it holds, and both sides converge afterwards

### Requirement: Scenario time is owned by the scenario

A scenario MUST advance simulated time explicitly and MUST NOT sleep. Delivery, timeouts and announcement intervals MUST all
be driven from that simulated time, so a long outage costs no real time.

#### Scenario: An hours-long outage runs instantly

- **WHEN** a scenario simulates an hour of partition
- **THEN** it completes in real time proportional to the message count, not to the simulated duration

### Requirement: The convergence property is checked across a seed sweep

The harness MUST run a convergence scenario across many seeds and MUST report the number of runs, the first failing seed and
the failing assertion. A convergence claim MUST cite a sweep, not a single run.

#### Scenario: Sweep reports the failing seed

- **WHEN** one seed in a sweep fails to converge
- **THEN** the run fails with that seed and the assertion identified

#### Scenario: A clean sweep is reported as a count

- **WHEN** all seeds converge
- **THEN** the result states how many seeds ran, so a silently empty sweep cannot pass

### Requirement: The harness never becomes part of the shipped library

Scenario running MUST NOT add dependencies or transport code to the product library, and the product MUST keep building and
passing its unit tests with the simulation left out of the build.

#### Scenario: Seal the product library can exclude the harness

- **WHEN** the library is built without the simulation target
- **THEN** it still builds and its non-scenario tests pass

#### Scenario: No third-party dependency enters core..node

- **WHEN** the harness gains a feature
- **THEN** it stays inside the standard library and does not add a dependency to any product module
