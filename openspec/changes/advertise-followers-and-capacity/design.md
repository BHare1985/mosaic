# Design

## Context

`HelloMsg{version, patterns}` is encoded, decoded, golden-vector-tested and version-checked, and both ends of the wire ignore
the patterns. `Node::greet_peers` sends `HelloMsg{kProtocolVersion, {}}`; `Node::handle_hello` records the version and nothing
else. Meanwhile `JobWorker`'s constructor takes `std::vector<NodeId> workers` with the comment "JOBS-4 would derive it from
hello patterns", and `Node` already keeps `peer_links_` per peer (version, hello timing) - the table this information belongs
in.

## Decisions

### D1 - The patterns travel in the hello, not in a new message

The field exists and is tested; sending it is the smallest change that unblocks jobs. A second message (`SubscribeMsg`) would
mean a second retry schedule, a second staleness rule and a second thing to bound. The hello is already retried on the head
interval, which is the right cadence for a hint.

### D2 - The wire format changes, so the protocol version goes to 2

`HelloMsg` gains a capacity byte, and a peer that omits `patterns` cannot be distinguished from one that sent none; a version
number is how the decoder already refuses to guess. Version 1 peers are refused payloads by the existing check, counted as
`version_rejects`. This is a clean break by design: Mosaic is `0.x` and the mismatch is reported rather than mysterious.

Capacity is one byte, `0..255`, sent as "how many units of work I could start right now", clamped by the sender. One byte rather
than a float: a coarse hint that cannot invite arithmetic arguments. Zero means "prefer another", never "do not send".

### D3 - Per-peer follow sets live in `peer_links_`, and are replaced wholesale

`handle_hello` assigns rather than merges: a peer that unfollows must be able to say so. `Node` exposes
`peer_patterns(peer)` and `peers_following(pattern)`; both read the same table. The set is bounded by
`NodeConfig::max_head_patterns`, the limit already used for `GetHeadsMsg`, applied by truncation plus a counter - a hello that
exceeds the limit is a bug or an attack, and neither deserves a refusal that hides the rest of the message.

Staleness: if a peer has not sent a hello for `kHelloStaleIntervals x head_interval_ms`, its patterns and capacity are treated as
unknown. Otherwise a worker that crashed last week would keep being ranked.

### D4 - `JobWorker` derives its set, and an explicit list still wins

`JobWorker(node, signer, hasher, handler)` - the derived set - plus the existing constructor as the override. The derived set is
recomputed on each `tick` from `peers_following(service_channel)`, with the node's own id added when it follows the service
itself (a node may be its own worker). Ranking then uses `rendezvous_rank(job_id, workers, bucket)` as today, with saturated
workers moved behind unsaturated ones while keeping a total order - moving, not filtering, so "everyone saturated" still
produces a first choice.

Why not filter: the job must land somewhere. Why not weight the hash: the rendezvous property (only the winning worker changes
when the set changes) is what keeps jobs from being re-run on every membership change, and a weight input would break it.

### D5 - Nothing in the delivery path reads an advertisement

`Transport::publish` decides delivery by the sender's own matcher and the receiver's subscription; write authorization is
`ChannelRules`; peer acceptability is CURVE pins. The advertisement is read only by ranking code which, if wrong, costs a
round trip and not correctness. This is a requirement, not a convention, because the temptation to "subscribe on behalf of a
peer" will appear the first time a message is lost.

## Risks

- **Capacity flapping**: a worker oscillating between 0 and 1 spare reorders ranking constantly. Mitigated by recomputing the
  advertised figure only on the hello interval (not per tick) and by the moving-not-filtering rule.
- **Version bump churn**: every peer must upgrade at once. Acceptable at `0.x`; the alternative is guessing whether an empty
  pattern list means "none" or "old".
- **Big follow sets**: a node following thousands of patterns sends a big hello every head interval. Bounded by truncation and
  by the existing `max_head_patterns`; the counter makes the case visible.
