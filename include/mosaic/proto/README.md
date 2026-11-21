# proto — wire messages

**Purpose:** the `Message` variant: `RecordMsg`, `DirectRecordMsg`, `HeadMsg`, `FetchMsg`, `AckMsg`, `GetHeadsMsg`,
`DirectHeadMsg`, `CreditMsg`, `HelloMsg`. Semantics: 02-protocol (`docs/design/02-protocol.md`).

**Status:** every message is encoded and decoded (`codec.h`), with permanent ids, golden vectors and a total
decoder, and `Node` handles all of them. `PeersMsg` (id 10) carries peer-list gossip: entries of id, both
endpoints, hops and age.

**Rules**
- Adding a message = struct here + entry in the variant + design doc row + handler (or an explicit no-op TODO in
  `Node::handle`) + round-trip test once ENC-1 exists.
- Messages carry no behaviour.
