# Spec Delta

## Purpose

Defines calling a service by name over the same logs the library already replicates. This delta replaces the caller-supplied
worker list with one derived from advertisement, and makes ranking capacity-aware.

## MODIFIED Requirements

### Requirement: A worker runs a job when ranking says it is its turn

The set of workers for a service MUST be derivable from the peers whose advertised subscriptions include that service's
channel. A caller MAY still supply an explicit set, which MUST win over the derived one, so that single-node deployments and
tests do not need a network. A worker MUST NOT run a job whose assigned slot belongs to another worker while that worker is
known to be alive and has not deferred.

#### Scenario: Workers find each other

- **GIVEN** three nodes following `svc.compress` and no explicit worker list
- **WHEN** a job is published on `svc.compress`
- **THEN** exactly one of them runs it

#### Scenario: An explicit list wins

- **GIVEN** a worker constructed with an explicit list of two ids
- **WHEN** the advertised followers differ
- **THEN** ranking uses the explicit list

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
