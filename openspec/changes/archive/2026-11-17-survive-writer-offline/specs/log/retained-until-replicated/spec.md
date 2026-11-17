# Spec Delta

## Purpose

Defines the promise that a record survives its writer: replicas acknowledge what they hold, and a writer keeps records
until enough of them have.

## ADDED Requirements

### Requirement: A replica acknowledges the sequence it holds

A replica MUST report the highest sequence it has durably stored for a log, to the writer of that log, after accepting records.
Acknowledgements MUST be idempotent and MUST NOT be trusted for any sequence the replica has not stored.

#### Scenario: Storing a record produces an acknowledgement

- **WHEN** a replica accepts and stores sequence n of a writer's log
- **THEN** the writer receives an acknowledgement that covers n

#### Scenario: A duplicate acknowledgement changes nothing

- **WHEN** the same acknowledgement arrives repeatedly
- **THEN** the writer's view of that replica does not regress or double count

#### Scenario: A dishonest acknowledgement cannot delete data early

- **WHEN** a replica claims a sequence it did not store
- **THEN** the writer's retention decision is unaffected for records that no other replica confirmed

### Requirement: A writer retains records until enough replicas hold them

A writer MUST keep a record until at least `min_replicas` distinct replicas have acknowledged it, or until a configured
maximum retention window or size is exceeded, whichever comes first. A writer MUST expose which records it is still retaining
and which replicas it is waiting for.

#### Scenario: Records are released once replicated

- **WHEN** `min_replicas` distinct replicas have acknowledged sequence n
- **THEN** the writer may release sequence n from its own retention

#### Scenario: An unreachable replica keeps records retained

- **WHEN** no replica acknowledges because none can be reached
- **THEN** the writer keeps the records and reports that it is waiting, rather than assuming success

#### Scenario: Retention is bounded even without replicas

- **WHEN** the retention limit is exceeded with `min_replicas` unfulfilled
- **THEN** the oldest records are released and the shortfall is reported, so a misconfiguration is visible instead of fatal

### Requirement: `min_replicas` defaults to zero and must be chosen

A node MUST default `min_replicas` to zero - replication is best effort unless asked otherwise - and MUST make the setting
per-log so one deployment can protect audit logs while another keeps the default.

#### Scenario: Default does not retain

- **WHEN** `min_replicas` is left at its default
- **THEN** a writer releases records after publishing and accepts that they may exist only locally

#### Scenario: A configured floor is enforced per log

- **WHEN** a log is configured with `min_replicas = 2`
- **THEN** that log's records are retained until two replicas acknowledge them, while other logs on the same node are not
