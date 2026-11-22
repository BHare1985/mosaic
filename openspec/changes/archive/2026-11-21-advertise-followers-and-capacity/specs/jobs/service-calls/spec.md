# Spec Delta

## Purpose

Defines calling a service by name over the same logs the library already replicates. This delta replaces the caller-supplied
worker list with one derived from advertisement, and makes ranking capacity-aware.

## MODIFIED Requirements

### Requirement: Exactly one worker runs a job in the common case

Workers MUST rank themselves for a job id with rendezvous ranking over the worker set they know, and the highest-ranked worker
MUST run the job. Ranking MUST be deterministic from the job id and the worker set alone, so no coordination is needed. A worker
MUST be able to derive that set from the peers whose advertised subscriptions include the service's channel; an explicit set
supplied by the caller MUST win over the derived one, so a single-node deployment needs no network.

#### Scenario: All healthy workers pick the same runner

- **WHEN** three workers see the same request and the same worker set
- **THEN** exactly one runs it and the others do not

#### Scenario: Removing a worker only reassigns its own jobs

- **WHEN** a worker leaves the set
- **THEN** jobs it did not rank first for are unaffected

#### Scenario: Workers find each other

- **GIVEN** three nodes following `svc.compress` and no explicit worker list
- **WHEN** a job is published on `svc.compress`
- **THEN** exactly one of them runs it

#### Scenario: An explicit list wins

- **GIVEN** a worker constructed with an explicit list of two ids
- **WHEN** the advertised followers differ
- **THEN** ranking uses the explicit list

## ADDED Requirements

### Requirement: Ranking accounts for advertised capacity

When several workers are eligible, ranking MUST prefer those advertising spare capacity, and MUST fall back to plain
rendezvous order when none does. Capacity MUST NOT reorder workers in a way that makes two workers run the same job
simultaneously; it selects who is asked first, and the existing rank-and-wait rule still decides who runs.

#### Scenario: The overloaded worker is not asked first

- **GIVEN** rank 0 saturated and rank 1 idle
- **WHEN** a job is published
- **THEN** rank 1 is asked first and runs it, and rank 0 does not also run it

#### Scenario: Capacity does not cause duplicate execution

- **GIVEN** capacity information that changes between two ticks
- **WHEN** the job's deadline passes
- **THEN** at most one worker runs it per requested attempt
