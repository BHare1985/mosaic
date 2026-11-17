#pragma once

#include <functional>
#include <map>
#include <memory>
#include <mosaic/core/crypto.h>
#include <mosaic/flow/credits.h>
#include <mosaic/log/log.h>
#include <mosaic/match/matcher.h>
#include <mosaic/net/transport.h>
#include <mosaic/store/log_store.h>

namespace mosaic {

struct NodeConfig {
    NodeId id;
    std::uint32_t head_interval_ms = 1000; // how often a writer announces its head
    std::uint32_t fetch_batch = 64; // most records requested by one FETCH
    std::uint32_t fetch_window = 256; // most records we allow in flight from one peer (credit)
    std::uint32_t fetch_timeout_ms = 500; // presume a FETCH lost after this long
    std::uint32_t max_serve = 1024; // most records we send for one FETCH, whatever the requester asks
    /// Replicas that must acknowledge a record before the writer stops retaining it. Zero means best
    /// effort: nothing is retained on a replica's account (see log/retained-until-replicated).
    std::uint32_t min_replicas = 0;
    /// Most records a writer keeps waiting for acknowledgements. Past this the oldest are released and
    /// the shortfall is counted, because blocking forever is an outage and silence is unnoticed loss.
    std::uint32_t retention_limit = 10000;
    /// How far behind a follower may fall before it stops taking live records and repairs by fetch.
    std::uint32_t slow_follower_threshold = 512;
    /// Minimum spacing for head requests and acknowledgements, per peer and per log.
    std::uint32_t control_interval_ms = 500;
    /// Largest payload this node will accept or publish (matches the wire format's record limit).
    std::uint32_t max_record_bytes = 256 * 1024;
    /// Most patterns a single head request may contain. A peer asking about everything costs a matcher build.
    std::uint32_t max_head_patterns = 64;
    /// Most requests of one kind a peer may have answered per control interval.
    std::uint32_t max_requests_per_interval = 32;
};

/// Cumulative counters, so tests assert on behaviour instead of eyeballing logs (NODE-1).
/// They are written only where a decision has already been made: no counter may change control flow.
struct NodeCounters {
    std::uint64_t accepted = 0;
    std::uint64_t duplicate = 0;
    std::uint64_t gap = 0;
    std::uint64_t fork = 0;
    std::uint64_t bad_signature = 0;
    std::uint64_t bad_chain = 0;
    std::uint64_t fetches_sent = 0;
    std::uint64_t fetch_retries = 0; // requests abandoned on timeout and retried
    std::uint64_t credit_drops = 0; // a repair request we could not afford
    std::uint64_t version_rejects = 0; // payload refused: the peer announced a version we cannot speak
    std::uint64_t unnegotiated = 0; // payload accepted before the peer's version was known (see 02-protocol)
    std::uint64_t credit_spent = 0; // units committed to repair requests, cumulatively
    std::uint64_t credit_returned = 0; // units given back, cumulatively
    std::uint64_t acks_sent = 0; // acknowledgements we sent as a replica
    std::uint64_t acks_received = 0; // acknowledgements we received as a writer
    std::uint64_t retention_shortfall = 0; // records released without reaching min_replicas
    std::uint64_t heads_requested = 0; // GetHeads exchanges we started
    std::uint64_t heads_answered = 0; // GetHeads exchanges we served
    std::uint64_t slow_follower_switches = 0; // times we stopped trusting the live path for a log
    std::uint64_t oversized_records = 0; // refused: payload above max_record_bytes
    std::uint64_t head_requests_refused = 0; // refused: more patterns than a request may carry
    std::uint64_t requests_throttled = 0; // per-peer budgets: requests dropped for arriving too fast
};

/// The glue that makes the building blocks one system. A Node is simultaneously:
///   - a WRITER for the logs it publishes (HEAD heartbeats),
///   - a REPLICA for every channel pattern it follows (verify, store, repair gaps via FETCH),
///   - a RESPONDER that serves FETCH from its store, including logs written by others.
/// There is no separate "store" or "consumer" role: a store is a node that follows ">" and keeps everything.
///
/// Single-threaded and non-blocking. Time is injected (`now_ms`) so tests are deterministic.
/// Typical loop:   node.pump(now); node.tick(now);
/// Design: docs/design/05-node-and-transport.md, protocol: docs/design/02-protocol.md.
class Node {
public:
    using RecordHandler = std::function<void(const Record&)>;

    /// All references must outlive the Node.
    Node(NodeConfig config, Transport& transport, LogStore& store, const Hasher& hasher, const Verifier& verifier);

    /// Start following channels matching `pattern` (see Matcher). Returns false for an invalid pattern.
    bool follow(const std::string& pattern);

    /// Called once per record, in log order, for records accepted from the network (not for own publishes).
    void on_record(RecordHandler handler) { on_record_ = std::move(handler); }
    /// Grant live-delivery credit to a peer. Until a grant exists the live path stays best effort.
    void grant_live_credit(const NodeId& peer, std::uint32_t credits);

    /// Records this writer still holds waiting for acknowledgements, across all logs it writes.
    std::uint32_t retained_records() const;
    /// Logs whose retention floor cannot be met within the configured limit.
    std::vector<LogKey> retention_shortfalls() const;

    /// Evidence of writer equivocation this node holds (LOG-1), and the count of evidence dropped past the
    /// configured bound. Kept so a fork can be shown later, not only reported once.
    std::vector<ForkEvidence> forks() const { return replica_.forks(); }
    void set_max_fork_evidence(std::size_t max) { replica_.max_evidence = max; }
    std::uint64_t fork_evidence_overflow() const { return replica_.evidence_overflow; }

    /// Called when a record proves the writer equivocated (same seq/prev, different content).
    void on_fork(RecordHandler handler) { on_fork_ = std::move(handler); }

    /// Append to our own log on `channel` and announce it. The signer must outlive the Node.
    /// Empty when the payload exceeds the configured limit - a caller must not be able to create a log its
    /// own peers would refuse (bound-peer-requests).
    std::optional<Record> publish(const Signer& signer, const Channel& channel, std::string content_type, Bytes payload,
        std::uint64_t now_ms);

    /// Handle everything waiting in the transport.
    void pump(std::uint64_t now_ms);
    /// Run timers: HEAD heartbeats, FETCH timeouts and retries.
    void tick(std::uint64_t now_ms);

    std::optional<Head> head(const LogKey& key) const { return replica_.head(key); }
    const NodeId& id() const { return config_.id; }
    const NodeCounters& counters() const { return counters_; }

    /// Credit still spendable on `peer`. Together with the cumulative `credit_spent`/`credit_returned`
    /// counters this must satisfy `available == fetch_window - (spent - returned)` on every step: credit
    /// is either spendable or committed, never both (FLOW-2).
    std::uint32_t credit_available(const NodeId& peer) const;

private:
    struct WriterSlot {
        std::unique_ptr<LogWriter> log;
        std::uint64_t last_head_ms = 0;
    };
    struct PeerLink {
        std::uint64_t last_hello_ms = 0;
        bool hello_sent = false;
        bool version_known = false;
        std::uint8_t version = 0;
    };
    struct FetchState {
        std::uint64_t sent_ms = 0;
        Seq from = 0; // first seq requested
        Seq end_seq = 0; // last seq requested
        std::uint32_t credit_left = 0; // credit this request has not accounted for yet
    };

    void handle(const NodeId& from, const Message& message, std::uint64_t now_ms);
    void handle_record(const NodeId& from, const Record& record, bool direct, std::uint64_t now_ms);
    void handle_head(const NodeId& from, const HeadMsg& head, std::uint64_t now_ms);
    void handle_fetch(const NodeId& from, const FetchMsg& fetch, std::uint64_t now_ms);
    void handle_ack(const NodeId& from, const AckMsg& ack);
    void handle_get_heads(const NodeId& from, const GetHeadsMsg& request, std::uint64_t now_ms);
    void handle_direct_head(const NodeId& from, const DirectHeadMsg& head, std::uint64_t now_ms);
    void handle_credit(const NodeId& from, const CreditMsg& credit);
    /// Per-peer budget: true when this peer may have another request of this kind right now.
    bool admit_request(const NodeId& peer, std::uint64_t now_ms);
    /// Announce what we hold for the patterns we follow, so a newcomer can find logs without the writer.
    void request_heads(std::uint64_t now_ms);
    /// Send an acknowledgement for logs we hold, rate-limited per peer.
    void acknowledge(std::uint64_t now_ms);
    /// Advance the retention floor for a log if enough replicas have acknowledged it.
    void advance_retention(const LogKey& key);
    /// How many sequences behind the highest head we know of for this log.
    Seq behind_by(const LogKey& key) const;
    void handle_hello(const NodeId& from, const HelloMsg& hello);
    /// Send our version to any peer we have not confirmed a version for yet. Called from tick().
    void greet_peers(std::uint64_t now_ms);
    /// Is payload from this peer allowed? False (and counted) when it announced a version we cannot speak.
    bool accept_payload(const NodeId& peer);
    void request_fetch(const LogKey& key, const NodeId& peer, std::uint64_t now_ms);
    /// Return one credit for a repair record that answers a live request. A record belonging to a
    /// request that already ended (late response) is consumed and must not create credit (FLOW-2).
    void consume_credit(const NodeId& peer, const LogKey& key, Seq seq);
    void return_credit(const NodeId& peer, std::uint32_t amount);
    Seq next_seq(const LogKey& key) const;
    CreditWindow& credits_for(const NodeId& peer);

    NodeConfig config_;
    Transport& transport_;
    LogStore& store_;
    const Hasher& hasher_;
    ReplicaLog replica_;
    Matcher following_;

    std::map<LogKey, WriterSlot> writers_;
    std::map<LogKey, Seq> remote_head_; // highest seq we have heard of for each log
    std::map<std::pair<LogKey, NodeId>, FetchState> fetches_;
    std::map<NodeId, CreditWindow> peer_credits_;
    std::map<NodeId, PeerLink> peer_links_;
    std::map<std::pair<LogKey, NodeId>, Seq> acked_; // highest sequence each peer confirmed per log
    std::map<LogKey, Seq> released_through_; // retention floor: records at or below this are released
    std::map<LogKey, std::uint64_t> shortfall_at_; // when a log last hit the retention limit
    std::map<NodeId, CreditWindow> live_credits_; // live-path credit, opt-in per peer
    std::map<NodeId, bool> live_credit_enabled_;
    std::map<LogKey, std::uint64_t> last_ack_ms_;
    std::map<LogKey, NodeId> source_peer_; // where a log's records actually came from (acks go there)
    std::map<NodeId, std::uint64_t> last_heads_request_ms_;
    std::vector<std::string> patterns_; // what we follow, replayed to peers when asking for heads
    std::map<NodeId, std::pair<std::uint64_t, std::uint32_t>> request_budget_; // peer -> (window start, used)
    NodeCounters counters_;
    RecordHandler on_record_;
    RecordHandler on_fork_;
};

} // namespace mosaic
