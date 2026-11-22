# Proposal

## Why

Discovery today is two mechanisms with a gap between them: a static peer list, and a LAN beacon. Both work, and both stop working
in the case that matters - a network where nodes are not on the same link layer and the operator does not want to list every node
in every config. Remove one node from such a deployment and the nodes that only learned about each other through it lose each
other.

`discover-peers-with-czmq` tried czmq's `zgossip` and had to drop it: its event stream is a private zproto frame format, so a node
could join the gossip ring only by depending on czmq inside `net`, which the dependency rules forbid (decision D4, and
`docs/upstream-notes.md` records the finding). DISC-3 is the same capability built on our own protocol.

## What Changes

- A new message: `PeersMsg`, carrying a bounded list of `(node id, pub endpoint, router endpoint)` entries the sender knows.
- Gossip on a timer: every node sends `PeersMsg` to a small random subset of its known peers, with a hop limit and an age per entry,
  so an entry propagates and expires instead of circulating forever.
- Received entries are **hints**: they are added to the same discovery table the beacon feeds, they still have to pass the CURVE
  pin policy before a link is used, and they never displace a configured peer's settings.
- The table is bounded: a maximum number of entries, LRU eviction, and a per-message cap. A peer that gossips a thousand fake
  entries costs itself a strike, not the receiver's memory.

## Capabilities

### New Capabilities

- None.

### Modified Capabilities

- `discovery/peer-discovery`: peer lists may be learned from peers, with the same "hints, never authority" rule as the beacon.
- `proto/wire-format`: one new message id, with a total decoder and a golden vector.
