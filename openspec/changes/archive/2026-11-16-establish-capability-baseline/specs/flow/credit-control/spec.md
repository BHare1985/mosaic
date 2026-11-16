# Spec Delta

## Purpose

Defines receiver-controlled pacing so that a slow or greedy peer can never make a sender queue without bound or slow anyone else down.

## ADDED Requirements

### Requirement: Credit is granted by the receiver and spent by the sender

A sender MUST NOT transmit a unit on a credit-controlled path without holding credit for it. Credit MUST be non-negative and MUST never exceed the window's capacity; grants beyond capacity MUST be clamped rather than accumulated.

#### Scenario: Sending stops at zero credit

- **WHEN** a peer holds zero remaining credit and another unit would be sent
- **THEN** nothing is sent and no bits leave the sender

#### Scenario: Grants never exceed capacity

- **WHEN** repeated grants arrive for a window that is already full
- **THEN** the available credit stays clamped at capacity

#### Scenario: Spending is bounded by available credit

- **WHEN** a sender asks for more units than are available
- **THEN** it receives at most the available amount, and availability afterwards is non-negative

### Requirement: Repair traffic is credit-gated on both ends

A repair request MUST ask for no more records than the requester's available credit for that peer, and a responder MUST serve no more records than one request allows and no more than its own per-response cap. Each repair record returned MUST consume one credit.

#### Scenario: Request size is limited by credit

- **WHEN** a requester holds 3 units of credit and 50 records are missing
- **THEN** the request asks for at most 3 records

#### Scenario: Responder caps the response

- **WHEN** a request asks for more records than the responder's per-response cap allows
- **THEN** the responder returns only up to its cap

#### Scenario: Each returned record consumes one credit

- **WHEN** a repair record arrives for an outstanding request
- **THEN** exactly one unit of credit is consumed for it

### Requirement: Unanswered requests must not inflate credit

If a repair response never arrives, the requester MUST eventually reclaim capacity so repair can be retried, and MUST NOT permit a peer's outstanding debt to exceed the window capacity. Correct accounting is per outstanding request, not a blanket reset of the peer's window.

#### Scenario: Lost response is retried without over-crediting

- **WHEN** a repair request times out and is retried
- **THEN** total outstanding requests to that peer never exceed the window's capacity

#### Scenario: Late responses do not create credit

- **WHEN** a response arrives for a request already reclaimed by timeout
- **THEN** it does not increase the peer's available credit above capacity

### Requirement: Live delivery is best effort and must never block a writer

The live delivery path MUST NOT apply back-pressure to the writer or to other followers. A follower that cannot keep up MUST lose live records and recover them through the repair path rather than slowing the system down.

#### Scenario: A slow follower does not slow the writer

- **WHEN** one follower stops consuming while others continue
- **THEN** publication rate is unaffected and the stopped follower recovers later by repair

#### Scenario: Dropped live records are recoverable

- **WHEN** a follower misses live records
- **THEN** the follower converges to the writer's log through repair alone

### Requirement: Memory per peer is bounded

Per-peer state, including outstanding requests and in-flight records, MUST be bounded by configuration rather than by peer behaviour, so a malicious or broken peer cannot exhaust memory.

#### Scenario: Outstanding work is capped per peer

- **WHEN** a peer never answers and keeps requesting
- **THEN** the requester's tracked outstanding work stays within its configured bound
