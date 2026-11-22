# security/abuse-limits Specification

## Purpose
Defines what an authenticated peer may cost a node: the size of a record, the number of patterns it may ask about, and how much
work it may queue - so that a well-behaved network cannot be hurt by one peer holding a valid key.

## Requirements

### Requirement: Record size is bounded before it is stored

A record larger than the configured maximum MUST be rejected at ingest and refused at publish, before any store write, and the
rejection MUST be reported rather than counted as a silent drop.

#### Scenario: An oversized record is refused on the wire path

- **WHEN** a peer sends a record whose payload exceeds the limit
- **THEN** it is not stored, not delivered to the application, and the rejection is visible

#### Scenario: A local publisher cannot bypass the limit

- **WHEN** a local caller publishes a payload over the limit
- **THEN** the publish reports the refusal instead of writing it

### Requirement: Pattern and query work per request is capped

A node MUST cap the number of patterns it will process in one head request, and MUST limit the rate of repair and head requests it
answers per peer. Requests beyond the cap MUST be refused, not partially served.

#### Scenario: A hostile head request is refused whole

- **WHEN** a peer asks about more patterns than the cap allows
- **THEN** the request is refused and no matcher is built for it

#### Scenario: Repeat requests are throttled

- **WHEN** a peer asks for heads far more often than the configured interval
- **THEN** the surplus requests are dropped and counted, and the peer is not disconnected

### Requirement: Queued work per peer is bounded

A node MUST bound the work a single peer can queue - outstanding repair requests and, in the job layer, pending jobs. Exceeding a
bound MUST be counted so the condition is observable.

#### Scenario: A flooding peer cannot grow the queue

- **WHEN** a peer sends requests faster than they are retired
- **THEN** the node's queued work stays within its bound and the overflow is counted

#### Scenario: Bounded work still converges

- **WHEN** a legitimate peer is throttled because it was briefly bursty
- **THEN** it recovers and converges without operator action
