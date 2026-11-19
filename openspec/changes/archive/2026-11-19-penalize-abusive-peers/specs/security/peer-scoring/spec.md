# Spec Delta

## Purpose

Defines what happens after a node has already counted a peer's faults: strikes, decay, escalating bans, and the boundary
between "we stop listening to you" (local policy) and "we stop believing you" (never - that is what signatures are for).

## ADDED Requirements

### Requirement: Objective faults earn strikes

A node MUST award strikes for faults that require no judgement: a message that does not decode, a record whose signature or
hash chain does not verify, a record exceeding the size limit, a record nobody asked for, and a hello announcing an
unsupported protocol version. A node MUST NOT award strikes for late, missing or duplicated traffic, or for a peer falling
behind - those are network conditions, and punishing them would turn a partition into a ban.

#### Scenario: A forged record earns a strike

- **GIVEN** a peer sending a record whose signature does not verify
- **WHEN** the node handles it
- **THEN** the record is refused, one strike of that kind is recorded, and the peer is otherwise unaffected

#### Scenario: Loss and reordering earn nothing

- **GIVEN** a peer whose messages are delayed, dropped and duplicated by the network
- **WHEN** the node handles the traffic that does arrive
- **THEN** no strikes are awarded

### Requirement: Strikes decay and bans escalate

Strikes MUST decay so that occasional faults never accumulate into a ban. Crossing the strike threshold MUST ban the peer for a
base duration; crossing it again MUST at least double the duration, up to a configured maximum. A ban MUST expire on its own,
without an operator action, and MUST be lifted early by a successful, well-formed exchange.

#### Scenario: One bad frame is not a ban

- **GIVEN** a peer that sends one undecodable message and then behaves
- **WHEN** the decay window passes
- **THEN** its strikes return to zero and no ban is ever recorded

#### Scenario: Repeated faults escalate

- **GIVEN** a peer that crosses the threshold, waits out the ban, and crosses it again
- **WHEN** the second ban starts
- **THEN** its duration is at least double the first

### Requirement: A banned peer is ignored, not forgotten

While a ban is active the node MUST drop that peer's messages before decoding them, MUST NOT serve it repair, and MUST NOT offer
it jobs. It MUST keep its addresses, its frame counters and its place in the peer set, so that the ban expiring restores service
without a restart. Ban state MUST NOT extend to the data layer: no acceptance decision may depend on it.

#### Scenario: A banned peer asks for repair

- **GIVEN** an actively banned peer sending a fetch request
- **WHEN** the node pumps
- **THEN** the request is dropped, counted, and no records are sent in reply

#### Scenario: The ban expires and service resumes

- **GIVEN** a ban that has just expired and a peer that now sends valid traffic
- **WHEN** the node pumps
- **THEN** its records are handled as before, with no operator action

#### Scenario: A ban cannot change what is believed

- **GIVEN** a banned peer that sends a correctly signed record the node does not have
- **WHEN** the ban expires and the same record arrives on another link
- **THEN** it is accepted on its signature, because bans are not a way to reject data
