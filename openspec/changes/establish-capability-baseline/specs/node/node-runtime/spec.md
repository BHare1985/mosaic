# Spec Delta

## Purpose

Defines the single node that publishes its own logs, replicates the logs it follows, and serves missing records to peers, converging without a broker or consensus.

## ADDED Requirements

### Requirement: One node serves as writer, replica and responder

A node MUST be able to publish its own signed logs, follow channel patterns to replicate other writers' logs, and answer repair requests for any log it holds. These roles MUST coexist in one instance without interference.

#### Scenario: A node publishes and follows at once

- **WHEN** a node publishes on its own channel while following another writer's channel
- **THEN** it both extends its own log and stores the other writer's records

#### Scenario: Any holder can answer a repair request

- **WHEN** a follower requests records from a peer that holds them but did not write them
- **THEN** that peer serves the records

### Requirement: Following a pattern registers it locally and with the transport

Following MUST add the pattern to the node's own matching state and subscribe the transport to it, so more messages are not delivered than the node will accept, and the node can still re-check what it receives.

#### Scenario: Follow causes delivery to begin

- **WHEN** a node follows events.> and a writer publishes on events.x
- **THEN** the node receives the record

### Requirement: Live records are verified before they are stored or handed to the app

An arriving record for a followed channel MUST be verified and stored before the application callback fires, and the callback MUST fire at most once per sequence per log.

#### Scenario: Verified record reaches the app once

- **WHEN** a live record arrives, is valid, and is stored
- **THEN** the application callback fires once for that sequence

#### Scenario: Forged record is rejected and reported

- **WHEN** a record with an invalid signature arrives
- **THEN** it is not stored, the application callback does not fire, and a rejection is reported

### Requirement: Writers announce their head periodically

A writer MUST announce its current head sequence and hash at a configured interval. A follower that is behind an announced head MUST begin repair, so tail loss is repaired even when no later record arrives.

#### Scenario: A follower behind the head repairs

- **WHEN** a head announcement reports a higher sequence than the follower holds
- **THEN** the follower requests the missing range from a peer that can serve it

#### Scenario: A late joiner catches up while the writer continues

- **WHEN** a node starts following after 200 records exist
- **THEN** it reaches the writer's head and then keeps up with live records

### Requirement: Gaps are repaired from the sender or from any peer

When a record reveals a missing range, the node MUST request that range, MUST NOT duplicate an already outstanding request for the same range to the same peer, and MUST respect its credit and batch limits. After a timeout the request MUST be considered abandoned and retriable.

#### Scenario: Missing range is requested once

- **WHEN** several records reveal the same missing range before a response arrives
- **THEN** only one request for that range is outstanding per peer

#### Scenario: Request respects batch and credit limits

- **WHEN** a range larger than the configured batch or available credit is missing
- **THEN** the request covers at most that bound, and the remainder is requested afterwards

#### Scenario: A timed-out request is retried

- **WHEN** a repair response never arrives
- **THEN** the request is abandoned after the timeout and retried, and the follower still converges

### Requirement: All time-dependent behaviour is driven by an injected clock

A node MUST NOT read a clock itself. All interval and timeout behaviour MUST be driven by caller-supplied timestamps passed to its pump and tick steps, so behaviour is deterministic and testable.

#### Scenario: No internal clock is consulted

- **WHEN** identical message sequences are replayed with identical timestamps
- **THEN** the node takes identical decisions

#### Scenario: Head announcement follows supplied time

- **WHEN** callers advance time past the announcement interval and invoke the tick step
- **THEN** exactly one head announcement is published for that interval

### Requirement: Followers converge to the writer's log under loss and duplication

After publication stops, every follower MUST hold exactly the writer's records, in order, for every log it follows - and only those - regardless of loss and duplication during publication. No unverified record may be stored or delivered at any point.

#### Scenario: Convergence under loss and duplication

- **WHEN** records are published while 30 percent of messages are lost and 10 percent duplicated
- **THEN** every follower's head equals the writer's head for each followed log

#### Scenario: Non-followed channels stay out

- **WHEN** a follower follows events.a and the writer also publishes on events.b
- **THEN** the follower holds no records for events.b

### Requirement: Contradictory history is surfaced, not silently accepted

When a node detects equivocation it MUST report the conflict to the application and MUST keep serving the chain it already accepted, so a fork is visible rather than blended into history.

#### Scenario: Equivocating writer is reported

- **WHEN** a writer publishes two different valid records for the same sequence
- **THEN** the application is notified of the fork and the stored chain is unchanged
