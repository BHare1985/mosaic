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
    /// Called when a record proves the writer equivocated (same seq/prev, different content).
    void on_fork(RecordHandler handler) { on_fork_ = std::move(handler); }

    /// Append to our own log on `channel` and announce it. The signer must outlive the Node.
    Record publish(const Signer& signer, const Channel& channel, std::string content_type, Bytes payload, std::uint64_t now_ms);

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
    void handle_fetch(const NodeId& from, const FetchMsg& fetch);
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
    NodeCounters counters_;
    RecordHandler on_record_;
    RecordHandler on_fork_;
};

} // namespace mosaic
