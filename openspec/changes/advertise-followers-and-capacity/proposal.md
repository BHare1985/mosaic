# Proposal

## Why

Two things are missing and they are the same thing: a node cannot see what its peers care about. `HelloMsg` already has a
`patterns` field - it is encoded, decoded and tested - but `Node::greet_peers` sends it empty and `Node::handle_hello` throws
the patterns away. So `JobWorker` has to be handed a static list of worker ids by whoever built it
(`JobWorker(node, signer, hasher, workers, ...)`), which is exactly the coordination the jobs layer exists to avoid, and
JOBS-4 (capacity-aware ranking) cannot start because there is no way to learn who is overloaded.

## What Changes

- `HelloMsg` carries the sender's subscription patterns, bounded and deduplicated, and a spare-capacity figure.
- `Node` keeps a per-peer follow set and exposes it (`peer_patterns(peer)`, `peers_following(pattern)`), and a per-peer capacity
  figure. Both are **hints**: a peer that under-reports or lies about its patterns gains nothing, because matching and
  authorization still decide what is delivered.
- `JobWorker` derives its worker set from the peers following `svc.<service>` instead of requiring a list, and skips workers
  advertising no spare capacity. The static-list constructor stays as an override for tests and single-node setups.
- The protocol version bumps: a peer advertising these fields speaks version 2, and version 1 peers are refused payloads as
  today (a reported, counted mismatch), which is what the version handshake is for.

## Capabilities

### New Capabilities

- `proto/peer-advertisement`: what a peer may learn about another peer's interests and capacity, and what it must not conclude
  from it.

### Modified Capabilities

- `jobs/service-calls`: the worker set is derived from advertised subscriptions rather than supplied by the caller, and
  ranking skips saturated workers.
