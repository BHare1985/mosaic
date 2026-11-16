# net/transport Specification

## Purpose
Defines the delivery contract between nodes, so the node runtime is written once against a contract instead of once per transport.

## Requirements

### Requirement: Transport delivery is best effort and never blocks

A transport MUST deliver messages on a best-effort basis: it MAY drop, duplicate or reorder, and it MUST NOT block or throw when it cannot deliver. Reliability of log content MUST come from the replication and repair layers above, never from the transport.

#### Scenario: An undeliverable publish does not block

- **WHEN** a publish is issued while no peer is reachable
- **THEN** the call returns without blocking and without error

#### Scenario: Loss is tolerated above the transport

- **WHEN** messages are dropped in transit
- **THEN** followers still converge through repair

### Requirement: A node never receives its own publication

A transport MUST NOT deliver a node's own publish back to that node, so writers do not have to filter themselves out.

#### Scenario: Publishing does not echo

- **WHEN** a node publishes on a channel it also subscribes to
- **THEN** it does not receive its own message

### Requirement: Publication is filtered by subscription

An implementation MUST deliver a published message only to peers whose subscription patterns match the channel. Conformance checks that a peer following an unrelated channel receives nothing.

#### Scenario: Unrelated subscribers receive nothing

- **WHEN** a message is published on events.a while another peer follows only metrics.>
- **THEN** the other peer receives nothing

### Requirement: Unicast to an unknown peer is dropped silently

A transport MUST accept a unicast call for any peer identity and MUST silently discard it when that identity is unknown or unreachable. Callers MUST NOT have to track peer reachability to send safely.

#### Scenario: Unicast to an absent peer is harmless

- **WHEN** a node unicasts to an identity that is not connected
- **THEN** the call returns and nothing is delivered anywhere

### Requirement: Node identity is stable for a transport's lifetime

A transport MUST expose a stable identity for the local node that peers can address. The identity MUST NOT change while the transport lives, because repair requests and credit accounting are keyed by it.

#### Scenario: Identity is addressable

- **WHEN** one node unicasts to another's exposed identity
- **THEN** exactly that node receives the message

### Requirement: Polling is non-blocking and yields at most one message

A transport MUST expose non-blocking polling that returns at most one inbound message per call, so the owning loop keeps control of time and ordering.

#### Scenario: Poll on an idle transport returns nothing

- **WHEN** no message has arrived
- **THEN** polling returns immediately with no message

### Requirement: The baseline transport is in-process and carries no bytes

The baseline transport implementation MUST connect nodes inside a single process only. It MUST NOT define a wire format and MUST NOT discover peers: encoding and discovery are separate capabilities, so no requirement here may assume bytes on a link or a topology the caller did not supply.

#### Scenario: All nodes share one process

- **WHEN** a scenario runs on the baseline transport
- **THEN** every node lives in the same process and no byte encoding is involved

#### Scenario: Peers are supplied, never searched for

- **WHEN** a node needs to reach a peer
- **THEN** the caller has already supplied that peer; the transport does not look for one

### Requirement: Every transport satisfies one conformance suite

All transport implementations MUST satisfy the same conformance suite, so the node runtime never needs transport-specific branches.

#### Scenario: A new transport runs the suite unchanged

- **WHEN** a transport implementation is added
- **THEN** it passes the existing conformance suite without modifying the suite

### Requirement: The conformance suite is reusable by any transport implementation

The transport conformance suite MUST be expressed against the transport contract and a factory that produces an
implementation, so a new transport is verified by supplying itself and no other change. The suite MUST cover best-effort
delivery, absence of self-delivery, publication filtering, silent drop of unicast to unknown peers, stable identity and
non-blocking poll.

#### Scenario: An implementation is added by supplying a factory

- **WHEN** a transport implementation is introduced
- **THEN** running the existing suite against it requires no edit to the suite

#### Scenario: A violating transport fails the suite

- **WHEN** an implementation delivers a node's own publication back to it
- **THEN** the suite fails on the self-delivery rule
