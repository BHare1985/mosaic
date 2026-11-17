# node/node-runtime Specification

## Purpose
Defines the single node that publishes its own logs, replicates the logs it follows, and serves missing records to peers, converging without a broker or consensus.

## Requirements

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

### Requirement: A node can run as a pure replica

A node MUST be usable with no application callback at all, following patterns and holding logs for others while serving repair
requests. This makes a store-only process and a tail-only process meaningful without inventing a separate component.

#### Scenario: A replica holds logs it never delivers

- **WHEN** a node follows a broad pattern with no application callback configured
- **THEN** it stores every matching log, answers repair requests for those logs, and never invokes a callback

#### Scenario: A second follower repairs from the pure replica

- **WHEN** a follower starts after the writer has gone offline and requests records from the pure replica
- **THEN** the follower converges from the replica without contacting the writer

### Requirement: Handshake failures are observable

A node MUST report, as values a test or operator can read, how many peers announced a protocol version it cannot speak and how
many messages were accepted before a peer's version was known. On real links a version mismatch is otherwise indistinguishable
from a quiet peer.

#### Scenario: A mismatched peer is visible

- **WHEN** a peer announces a version the node cannot speak
- **THEN** the rejection counter increases and the peer's payload stops being stored or delivered

#### Scenario: Late negotiation is counted, not hidden

- **WHEN** records arrive before the handshake has completed
- **THEN** they are processed and counted as unnegotiated, so the condition can be seen rather than inferred

### Requirement: Newcomers can learn heads from any peer

A node MUST be able to ask its peers for the heads of the logs matching a set of patterns, and MUST answer such a request from
its own store. This is what lets a log be found and repaired while its writer is offline.

#### Scenario: A newcomer finds a log with the writer away

- **WHEN** a node joins and the log's writer is not reachable, but a replica holds the log
- **THEN** the newcomer learns the head from that replica and repairs up to it

#### Scenario: A replica answers with what it actually holds

- **WHEN** a peer asks for heads matching a pattern
- **THEN** the node replies only for logs it holds, with the sequence and hash it has stored

### Requirement: Retention state is observable

A node MUST expose how many records it is retaining for replication, and for which logs it is below its `min_replicas` floor,
so an operator can see that durability is unmet instead of inferring it.

#### Scenario: Unmet replication is visible

- **WHEN** a writer is holding records because too few replicas acknowledged
- **THEN** a counter and the affected logs are readable without parsing logs

### Requirement: Following is reversible and publishing can batch

A node MUST be able to stop following a pattern, after which no further records for it are delivered or stored, and MUST be able
to publish several records in one call with the same ordering and signing guarantees as individual publishes.

#### Scenario: Unfollow stops delivery

- **WHEN** a node stops following a pattern
- **THEN** later records on matching channels are neither stored nor delivered

#### Scenario: A batch publishes in order

- **WHEN** several payloads are published in one call
- **THEN** they occupy consecutive sequences in the log, in the order given
