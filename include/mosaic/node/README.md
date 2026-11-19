# node — the glue

**Purpose:** `Node` wires Transport, LogStore, ReplicaLog, Matcher and CreditWindow into one peer that writes,
replicates, repairs and serves logs. Read `source/node/node.cpp` top to bottom (~170 lines) before changing
anything. It is the integration point of the whole design.

**Roles (all in one class):** writer (HEAD heartbeats), replica (verify/store/FETCH gaps), responder (serve FETCH).
A "store" is just a node that follows `>`.

**Rules**
- Single-threaded, non-blocking, time injected via `now_ms`. No sleeps, no threads, no globals.
- Message handling is a pure function of (state, message, now). That is what makes `SimNetwork` tests reproducible.
- Never trust a peer: every record goes through `ReplicaLog::ingest`.
- New behaviour needs a `SimNetwork` scenario in `test_node_sim.cpp`, and the counter it should move in `NodeCounters`.
- Credit is accounted **per request** (`FetchState::credit_left`), never by resetting a peer's window: the invariant
  `credit_available == fetch_window - (credit_spent - credit_returned)` must hold after every step (FLOW-2).
- Every inbound message goes through `admit()` first: that is the single choke point where a banned peer is dropped, so a new
  message kind cannot bypass a ban (SEC-2). Idiom for scoring: count the fault where it is already counted, then `penalize()`.
- **A ban is local policy, never belief.** No acceptance decision may look at a peer's score: a record is judged on its
  signature, whoever sent it and however badly they have behaved (see `docs/design/07-security.md`).

**Open work:** ACK-1, HEADS-1, HELLO-1, FLOW-3, FLOW-4, NODE-2, NODE-3.
