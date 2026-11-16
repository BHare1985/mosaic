# proto — wire messages

**Purpose:** the `Message` variant: `RecordMsg`, `DirectRecordMsg`, `HeadMsg`, `FetchMsg`, `AckMsg`, `GetHeadsMsg`,
`DirectHeadMsg`, `CreditMsg`, `HelloMsg`. Semantics: [02-protocol](../../../docs/design/02-protocol.md).

**Status:** values only. The binary encoder/decoder is ENC-1 and must be written before any real network
transport. `Node` currently handles Record, DirectRecord, Head, Fetch; the rest are TODO (ACK-1, HEADS-1,
FLOW-3, HELLO-1).

**Rules**
- Adding a message = struct here + entry in the variant + design doc row + handler (or an explicit no-op TODO in
  `Node::handle`) + round-trip test once ENC-1 exists.
- Messages carry no behaviour.
