# Proposal

## Why

A log is identified by `(writer identity, channel)` and the identity is a public key. That is the property that makes
provenance verifiable, and it is also a trap: keys leak, get rotated by policy, or are replaced when a host is rebuilt, and today
there is no way to say "the writer behind that key is now this key". The alternative - retire the channel and start a new log
with a new identity - breaks every follower's continuity and gives an attacker a cheap way to make real history look abandoned.

SEC-3 is the link record: the old key, still trusted, signs a statement naming its successor.

## What Changes

- A **rotation record**: a normal, signed, hash-chained record in the writer's own log with content type `mosaic/rotation`,
  whose payload names the successor public key and the sequence number at which the successor takes over.
- **Chain following** in the verifier side of the log: given a trusted identity and the store, a verifier can follow up to a
  bounded number of rotation links and answer "is identity B the legitimate successor of identity A for this channel".
- **Channel rules may name a rotation root**: an allow-list entry for a writer matches records from any identity in that writer's
  rotation chain, so a rotation does not silently require a rules change in every peer.
- `Mosaic rotate <channel> --to <new-key-file>` writes the link record and prints the new log key.

Rotation is opt-in per follower: a node follows chains only for writers it already trusts, and the chain is bounded, so a
rotated key cannot declare itself the ancestor of everything.

## Capabilities

### New Capabilities

- `core/key-rotation`: the rotation link, how far it is followed, and what a follower must refuse.

### Modified Capabilities

- `security/channel-authorization`: an allow-list entry may authorize a rotation chain rather than one identity.
