# Design

## Context

`Node` already counts nearly everything needed: `NodeCounters` has per-kind ingest results, `dropped`, `unnegotiated`,
`version_rejects`, `oversized_records`, and `request_budget_` meters requests per peer per window. What is missing is a place
that turns those events into a decision about a peer, and a single choke point where a peer can be skipped before work is done.

## Decisions

### D1 - Score in `Node`, at the point where a fault is already known

The natural place is where each fault is currently counted: `handle(from, message, now)`, `accept_payload`, the record ingest
path and `handle_hello`. One `PeerScore` object per peer in `peer_links_` alongside the version:

```cpp
struct PeerScore {
    std::uint32_t strikes = 0;
    std::uint64_t last_strike_ms = 0;
    std::uint32_t ban_count = 0;
    std::uint64_t banned_until_ms = 0;
};
```

Keeping it in the same table as the version and the hellos means one lifetime rule, one expiry sweep, and no second map to
prune when a peer leaves.

### D2 - Weights, not equality, and decay proportional to the window

Each fault kind gets a weight: undecodable frame 1, bad signature 3, oversized record 2, unrequested record 2, version mismatch
1. A bad signature costs more than a malformed frame because it means either a broken writer or an attacker, and a version
mismatch costs least because it is usually a deployment mistake, not an attack.

Decay is continuous rather than a step: strikes are reduced proportionally to elapsed time
(`strikes -= strikes * elapsed / decay_window`, floor 0) at each evaluation, so a peer that faults once a minute never
accumulates and one that faults a hundred times a second does. This is deliberately not a sliding window of counters: a window
needs history, and history needs memory per peer.

### D3 - Ban durations double, with a ceiling and a floor

`ban_ms = min(base_ms * 2^(ban_count - 1), max_ms)` with `base_ms = 5s`, `max_ms = 10min`. Doubling makes a persistent
attacker cheap to hold off and a flapping peer recoverable. Bans are per (node, peer) and never propagated: a peer cannot
poison another's view of a third party, which would be a denial-of-service vector in a system where nobody has authority over
anyone.

### D4 - One choke point: drop before decode

`handle()` starts with a ban check and returns before decoding. That single place covers records, heads, fetches, hellos and
credits, because every inbound path goes through it. Repair serving is a separate early return in the fetch handler, and job
ranking filters banned peers because ranking reads the same peer table.

Dropping before decoding matters: a banned peer should cost a map lookup and a counter, not a hash and a parse.

### D5 - The ban expires on its own, and early on evidence

A ban has an absolute deadline; the sweep that already expires advertisements also clears deadlines. Early lifting on a
well-formed exchange is safe because the only thing a ban can cost is the peer's own service; if it can complete a version
handshake and send a verifiable record, it is not the thing we banned.

### D6 - Bans never touch acceptance

`ReplicaLog::ingest`, signature verification and channel rules know nothing about scoring. This is a requirement in the spec
because the temptation is real: "this peer is banned, so its records must be bad" is exactly the reasoning that lets an
attacker make a node reject a good record by getting its *neighbour* banned.

## Risks

- **A buggy (not malicious) peer gets banned and the operator sees silence.** Mitigated by logging both edges of a ban and
  counting them; a ban is loud, not silent.
- **Thresholds that are too tight turn NAT reboots and clock skew into bans.** Nothing time-based awards strikes; only wrong
  bytes do.
- **Two maps of peer state drifting.** Avoided by keeping the score inside `peer_links_`.
