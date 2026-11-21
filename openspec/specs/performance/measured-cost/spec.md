# performance/measured-cost Specification

## Purpose
Defines what the library promises to know about its own cost - matching, per-tick node work, durable append - so that a
deployment can be sized from measurements instead of assumptions.

## Requirements

### Requirement: Per-tick work is independent of stored volume

The work a node does per tick MUST NOT scale with the number of records or logs it stores. It MAY scale with the logs it is
actively ingesting and the peers it knows.

#### Scenario: A large store does not slow the loop

- **WHEN** a node holds a hundred thousand records and ticks
- **THEN** the number of store queries per tick does not grow with the store size

#### Scenario: Acknowledgement follows ingest, not inventory

- **WHEN** nothing new has arrived for a log
- **THEN** no acknowledgement work is done for it

### Requirement: Retention cost is proportional to replicas, not to sequences

Computing how far a log may be released MUST depend on the number of acknowledgements, not on the sequence range between the
current floor and the head.

#### Scenario: A long unreplicated range does not cost a scan

- **WHEN** a log has a gap of a million unreleased sequences and three replicas acknowledge
- **THEN** the retention decision costs work proportional to three, not a million

### Requirement: Matching cost is measured and published

The repository MUST publish the cost of matching at 1k, 10k and 100k subscriptions, measured by a benchmark that runs in CI, and
MUST state the machine and compiler the figures came from. The published figures MUST be re-measured when the matcher changes, and
a change that worsens them MUST say so in its proposal rather than being discovered later.

#### Scenario: The numbers are reproducible

- **WHEN** the benchmark is run from the documented command on the reference configuration
- **THEN** it prints per-match cost at 1k, 10k and 100k subscriptions, and the same command appears in the docs

#### Scenario: A regression is caught

- **WHEN** a change makes matching an order of magnitude slower
- **THEN** the benchmark's asserted bound fails

#### Scenario: The published figures are reproducible

- **GIVEN** the benchmark and the documented command
- **WHEN** it is run on comparable hardware
- **THEN** it produces figures in the same order of magnitude as those published

#### Scenario: A regression is visible

- **GIVEN** a change that makes matching slower
- **WHEN** the benchmark runs
- **THEN** the difference appears in the figures the change publishes, not only in a review comment

### Requirement: Durability is a stated choice, not an accident

The repository MUST publish the cost of a durable append under each `Durability` mode and of batched ingest, and MUST state what
a crash may lose under each. A change that alters these costs MUST update the numbers.

#### Scenario: The fast mode is opt-in and documented

- **WHEN** an operator selects the batched mode
- **THEN** the documented guarantee states exactly what a crash can lose, and the default remains per-record

#### Scenario: The default keeps its promise

- **WHEN** a record is appended in the default mode and the process dies immediately
- **THEN** the record is present after reopening

#### Scenario: The batched cost is published

- **GIVEN** the batched ingest path
- **WHEN** the benchmark runs
- **THEN** the published figures give per-record cost for a batch, alongside the per-record durable append cost
