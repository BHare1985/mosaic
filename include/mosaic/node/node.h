#pragma once

#include <mosaic/core/crypto.h>
#include <mosaic/flow/credits.h>
#include <mosaic/log/log.h>
#include <mosaic/log/rotation.h>
#include <mosaic/match/matcher.h>
#include <mosaic/net/transport.h>
#include <mosaic/node/channel_rules.h>
#include <mosaic/store/log_store.h>

#include <functional>
#include <map>
#include <memory>
#include <set>

namespace mosaic {

/// How many hello intervals an advertisement stays usable. One lost hello must not make a peer
/// vanish from ranking, and a peer that has been silent for minutes must.
inline constexpr std::uint64_t kHelloStaleIntervals = 5;

/// Why a peer earned a strike. Only faults that are wrong whatever the network is doing: wrong
/// bytes. Lateness, loss, duplication and falling behind are network conditions and are never
/// faults, because punishing them would turn a partition into a ban (spec: security/peer-scoring).
enum class PeerFault {
    Undecodable,      // a frame that did not decode at all
    Unverified,       // a record whose signature or hash chain does not verify
    Oversized,        // a record above max_record_bytes
    Unrequested,      // a repair record nobody asked for
    VersionMismatch,  // a hello announcing a protocol version we cannot speak
};

const char* to_string(PeerFault fault);

/// What one fault costs a peer, in strikes. The single place these numbers live: a test asserts
/// this table and docs/design/07-security.md quotes it.
std::uint32_t strike_weight(PeerFault fault);

/// The edges of a ban. A ban is loud, not silent: the daemon prints one line per edge (SEC-2).
enum class BanEdge {
    Started,  // the strike threshold was crossed
    Expired,  // the ban lapsed on its own
    Lifted,   // a well-formed hello plus a verifiable record ended it early
};

struct BanEvent {
    NodeId peer;
    BanEdge edge = BanEdge::Started;
    std::uint32_t ban_count = 0;    // how many bans this peer has now had
    std::uint32_t strikes = 0;      // the strikes it carried when the edge fired
    std::uint64_t duration_ms = 0;  // Started: how long this ban lasts; otherwise 0
};

struct NodeConfig {
    NodeId id;
    std::uint32_t head_interval_ms = 1000;  // how often a writer announces its head
    std::uint32_t fetch_batch = 64;         // most records requested by one FETCH
    std::uint32_t fetch_window = 256;      // most records we allow in flight from one peer (credit)
    std::uint32_t fetch_timeout_ms = 500;  // presume a FETCH lost after this long
    std::uint32_t max_serve =
        1024;  // most records we send for one FETCH, whatever the requester asks
    /// Replicas that must acknowledge a record before the writer stops retaining it. Zero means
    /// best effort: nothing is retained on a replica's account (see log/retained-until-replicated).
    std::uint32_t min_replicas = 0;
    /// Most records a writer keeps waiting for acknowledgements. Past this the oldest are released
    /// and the shortfall is counted, because blocking forever is an outage and silence is unnoticed
    /// loss.
    std::uint32_t retention_limit = 10000;
    /// How far behind a follower may fall before it stops taking live records and repairs by fetch.
    std::uint32_t slow_follower_threshold = 512;
    /// Minimum spacing for head requests and acknowledgements, per peer and per log.
    std::uint32_t control_interval_ms = 500;
    /// Largest payload this node will accept or publish (matches the wire format's record limit).
    std::uint32_t max_record_bytes = 256 * 1024;
    /// Most patterns a single head request may contain. A peer asking about everything costs a
    /// matcher build.
    std::uint32_t max_head_patterns = 64;
    /// Most requests of one kind a peer may have answered per control interval.
    std::uint32_t max_requests_per_interval = 32;
    /// Strikes a peer may accumulate before it is banned. Zero disables banning: strikes are still
    /// counted, but no peer is ever refused (SEC-2).
    std::uint32_t strike_threshold = 8;
    /// How long a strike takes to decay to nothing. Decay is proportional to elapsed time, so a
    /// fault every window never accumulates and a flood does.
    std::uint32_t strike_decay_ms = 60000;
    /// First ban duration. Every further ban at least doubles it, up to `ban_max_ms`.
    std::uint32_t ban_base_ms = 5000;
    std::uint32_t ban_max_ms = 600000;
    /// Peer-list gossip (DISC-3). Zero `gossip_interval_ms` disables it entirely.
    std::uint32_t gossip_interval_ms = 5000;
    /// Neighbours offered the list each round. Two is enough for epidemic spread: fanout 2 reaches
    /// O(log N) rounds with high probability, one neighbour is fragile and three multiplies traffic
    /// for a property already satisfied.
    std::uint32_t gossip_fanout = 2;
    /// How far an entry may travel from its origin before it stops being propagated.
    std::uint32_t gossip_max_hops = 6;
    /// How long an entry stays in the table without being refreshed.
    std::uint32_t gossip_entry_ttl_ms = 30000;
    /// Retention policy for logs this node writes. Both limits are off by default: retention is the
    /// only operation that destroys data, so it is opt-in and evaluated as a policy rather than run
    /// on a timer (STORE-3).
    std::uint64_t retention_max_age_ms = 0;   // drop records older than this; 0 disables
    std::uint64_t retention_keep_min = 0;     // always keep this many records; 0 keeps none extra
    std::uint32_t retention_interval_ms = 0;  // how often the caller may evaluate; 0 disables
    /// Most peers the gossip table will hold. A beacon or a peer list is a hint, so the bound is
    /// what a flood costs.
    std::uint32_t gossip_table_limit = 256;
    /// Where this node's own PUB and ROUTER can be reached, so gossip can introduce it. Empty means
    /// "do not advertise myself", which is what a process with no addresses (the simulator) wants.
    /// A node that never advertised itself could only be found through a third party, and would
    /// vanish for everyone the moment that third party left.
    std::string pub_endpoint;
    std::string router_endpoint;
};

/// Cumulative counters, so tests assert on behaviour instead of eyeballing logs (NODE-1).
/// They are written only where a decision has already been made: no counter may change control
/// flow.
struct NodeCounters {
    std::uint64_t accepted = 0;
    std::uint64_t duplicate = 0;
    std::uint64_t gap = 0;
    std::uint64_t fork = 0;
    std::uint64_t bad_signature = 0;
    std::uint64_t bad_chain = 0;
    std::uint64_t fetches_sent = 0;
    std::uint64_t fetch_retries = 0;  // requests abandoned on timeout and retried
    std::uint64_t credit_drops = 0;   // a repair request we could not afford
    std::uint64_t version_rejects =
        0;  // payload refused: the peer announced a version we cannot speak
    std::uint64_t unnegotiated =
        0;  // payload accepted before the peer's version was known (see 02-protocol)
    std::uint64_t credit_spent = 0;            // units committed to repair requests, cumulatively
    std::uint64_t credit_returned = 0;         // units given back, cumulatively
    std::uint64_t acks_sent = 0;               // acknowledgements we sent as a replica
    std::uint64_t acks_received = 0;           // acknowledgements we received as a writer
    std::uint64_t retention_shortfall = 0;     // records released without reaching min_replicas
    std::uint64_t heads_requested = 0;         // GetHeads exchanges we started
    std::uint64_t heads_answered = 0;          // GetHeads exchanges we served
    std::uint64_t slow_follower_switches = 0;  // times we stopped trusting the live path for a log
    std::uint64_t oversized_records = 0;       // refused: payload above max_record_bytes
    std::uint64_t head_requests_refused = 0;   // refused: more patterns than a request may carry
    std::uint64_t requests_throttled =
        0;  // per-peer budgets: requests dropped for arriving too fast
    std::uint64_t unauthorized_records = 0;  // refused: a channel rule does not permit this writer
    std::uint64_t bans_started = 0;          // times this node banned a peer (SEC-2)
    std::uint64_t banned_drops = 0;          // messages refused because their sender was banned
    /// The one non-cumulative value here: peers banned right now. A ban lapsing lowers it, which is
    /// what makes "is the node still refusing anyone?" answerable without keeping a history.
    std::uint32_t peer_bans = 0;
    /// Strikes awarded per fault kind, cumulatively. Weighted, like the score: see strike_weight().
    std::map<PeerFault, std::uint64_t> strikes_by_kind;
    std::uint64_t peers_gossiped = 0;   // PeersMsg we sent
    std::uint64_t peers_learned = 0;    // entries that told us about an identity we did not know
    std::uint64_t peers_refreshed = 0;  // entries that confirmed something already in the table
    std::uint64_t peers_expired = 0;    // entries removed because nothing refreshed them
    std::uint64_t gossip_refused = 0;   // entries (or messages) refused: stale, too far, malformed
    std::uint64_t gossip_overflow = 0;  // entries dropped because the table was full
    std::uint64_t gossip_stale = 0;     // entries refused for being older than what we already hold
    std::uint64_t patterns_truncated = 0;      // advertised follow sets cut to the configured limit
    std::uint64_t capacity_unknown = 0;        // capacity reads that found a stale advertisement
    std::uint64_t hello_version_mismatch = 0;  // hellos announcing a version we cannot speak
    std::uint64_t retention_runs = 0;          // times the retention policy was evaluated
    std::uint64_t retention_dropped = 0;       // records dropped by retention
    std::uint64_t retention_refused = 0;       // logs kept because a condition was unknown or unmet
};

/// A peer this node heard about by gossip and cannot reach yet. The caller dials it: Node never
/// dials, because dialling is the transport's job (DISC-3).
struct GossipPeer {
    NodeId id;
    std::string pub_endpoint;
    std::string router_endpoint;
};

/// The glue that makes the building blocks one system. A Node is simultaneously:
///   - a WRITER for the logs it publishes (HEAD heartbeats),
///   - a REPLICA for every channel pattern it follows (verify, store, repair gaps via FETCH),
///   - a RESPONDER that serves FETCH from its store, including logs written by others.
/// There is no separate "store" or "consumer" role: a store is a node that follows ">" and keeps
/// everything.
///
/// Single-threaded and non-blocking. Time is injected (`now_ms`) so tests are deterministic.
/// Typical loop:   node.pump(now); node.tick(now);
/// Design: docs/design/05-node-and-transport.md, protocol: docs/design/02-protocol.md.
class Node {
public:
    using RecordHandler = std::function<void(const Record&)>;

    /// All references must outlive the Node.
    Node(NodeConfig config, Transport& transport, LogStore& store, const Hasher& hasher,
         const Verifier& verifier);

    /// Install channel write rules. Without one the node is open, which is the documented default.
    /// A rule is checked on ingest (before storage) and on publish (before the log is extended).
    void set_rules(const ChannelRules* rules) {
        rules_ = rules;
    }

    /// Rejections caused by a channel rule, per rule pattern - per writer would grow with the
    /// network.
    const std::map<std::string, std::uint64_t>& unauthorized_by_pattern() const {
        return unauthorized_by_pattern_;
    }

    /// Start following channels matching `pattern` (see Matcher). Returns false for an invalid
    /// pattern.
    bool follow(const std::string& pattern);

    /// Stop following a pattern. Records on matching channels are no longer stored or delivered.
    /// The transport subscription stays: `Transport` has no unsubscribe, so a peer may still send
    /// them and the node drops them.
    bool unfollow(const std::string& pattern);

    /// Patterns this node follows, for an operator or a test to read.
    std::vector<std::string> following() const {
        return following_.patterns();
    }

    /// What `peer` last advertised that it follows. Empty when it advertised nothing or the
    /// advertisement went stale - the two are deliberately indistinguishable to a caller, because
    /// neither may be trusted as authority (spec: proto/peer-advertisement).
    std::vector<std::string> peer_patterns(const NodeId& peer) const;

    /// Peers whose advertised follow set matches `channel`, for choosing whom to ask. A hint, never
    /// a delivery decision.
    std::vector<NodeId> peers_following(const Channel& channel) const;

    /// What `peer` advertised as spare capacity, or nothing when it advertises none or its
    /// advertisement is stale. Counts `capacity_unknown` when it has to guess, so the query is not
    /// const - the same shape as `authorized()`, which also counts what it decides.
    std::optional<std::uint8_t> peer_capacity(const NodeId& peer);

    /// Announce this much spare capacity in our next hello. One byte, coarse on purpose; zero means
    /// "prefer someone else", never "unavailable". The caller that knows its own backlog (the job
    /// worker) sets it; zero is the default so an unset node is never preferred over a measuring
    /// one.
    void set_spare_capacity(std::uint8_t spare) {
        spare_capacity_ = spare;
    }
    std::uint8_t spare_capacity() const {
        return spare_capacity_;
    }

    /// Publish several payloads to one channel. Same sequencing and signing as individual
    /// publishes, and it stops at the first refusal (an oversized payload), returning how many were
    /// written.
    std::size_t publish_batch(const Signer& signer, const Channel& channel,
                              const std::string& content_type, const std::vector<Bytes>& payloads,
                              std::uint64_t now_ms);

    /// Called once per record, in log order, for records accepted from the network (not for own
    /// publishes).
    void on_record(RecordHandler handler) {
        on_record_ = std::move(handler);
    }
    /// Grant live-delivery credit to a peer. Until a grant exists the live path stays best effort.
    void grant_live_credit(const NodeId& peer, std::uint32_t credits);

    /// Records this writer still holds waiting for acknowledgements, across all logs it writes.
    std::uint32_t retained_records() const;
    /// Logs whose retention floor cannot be met within the configured limit.
    std::vector<LogKey> retention_shortfalls() const;

    /// Evidence of writer equivocation this node holds (LOG-1), and the count of evidence dropped
    /// past the configured bound. Kept so a fork can be shown later, not only reported once.
    std::vector<ForkEvidence> forks() const {
        return replica_.forks();
    }
    void set_max_fork_evidence(std::size_t max) {
        replica_.max_evidence = max;
    }
    std::uint64_t fork_evidence_overflow() const {
        return replica_.evidence_overflow;
    }

    /// Called when a record proves the writer equivocated (same seq/prev, different content).
    void on_fork(RecordHandler handler) {
        on_fork_ = std::move(handler);
    }

    /// Called once per ban edge: when a peer is banned, when its ban lapses, and when it is lifted
    /// early. Lets an operator see the decision instead of inferring it from silence (SEC-2).
    void on_ban(std::function<void(const BanEvent&)> handler) {
        on_ban_ = std::move(handler);
    }

    /// Strikes `peer` carries at `now_ms`, after decay. Read-only: it never changes the score.
    std::uint32_t peer_strikes(const NodeId& peer, std::uint64_t now_ms) const;
    /// Is `peer` banned right now? A ban expires by itself, so this is the only correct way to ask:
    /// a deadline in the past reads as not banned even before the sweep has cleared it.
    bool peer_banned(const NodeId& peer, std::uint64_t now_ms) const;

    /// Identities whose addresses come from configuration. Gossip may confirm that such a peer
    /// exists, but must never record or use an address for it: a peer list is a hint, and a hint
    /// cannot move something an operator placed (spec: discovery/peer-discovery).
    void set_configured_peers(std::vector<NodeId> peers);
    /// Evaluate the retention policy for the logs this node writes: drop a prefix that is old and
    /// large enough, once replication has caught up, and leave a signed checkpoint at the new
    /// origin. Needs the writer's signer, because nobody else may say what the history at a
    /// sequence was.
    struct RetentionOutcome {
        std::size_t logs = 0;     // logs that lost records
        std::size_t dropped = 0;  // records gone
        std::size_t refused = 0;  // logs kept: a condition was unknown or unmet
    };
    RetentionOutcome retain(const Signer& signer, std::uint64_t now_ms);

    /// Peers learned by gossip that the transport does not address yet, least-recently offered
    /// first. A peer the transport already knows is never offered: a gossiped address must not be
    /// able to move a peer that configuration already placed.
    std::vector<GossipPeer> take_new_gossip_peers();
    /// Everything in the gossip table right now, for an operator or a test to read.
    std::vector<GossipPeer> gossip_peers() const;

    /// Append to our own log on `channel` and announce it. The signer must outlive the Node.
    /// Empty when the payload exceeds the configured limit - a caller must not be able to create a
    /// log its own peers would refuse (bound-peer-requests).
    std::optional<Record> publish(const Signer& signer, const Channel& channel,
                                  std::string content_type, Bytes payload, std::uint64_t now_ms);

    /// Handle everything waiting in the transport.
    void pump(std::uint64_t now_ms);
    /// Run timers: HEAD heartbeats, FETCH timeouts and retries.
    void tick(std::uint64_t now_ms);

    std::optional<Head> head(const LogKey& key) const {
        return replica_.head(key);
    }
    const NodeId& id() const {
        return config_.id;
    }
    const NodeCounters& counters() const {
        return counters_;
    }

    /// The last time the caller told us what time it is (from `pump`/`tick`). The node never reads
    /// a clock, so this is how a query about time-sensitive state - a stale advertisement, a ban
    /// that has lapsed - is answered without inventing one.
    std::uint64_t now_ms() const {
        return last_now_ms_;
    }

    /// Credit still spendable on `peer`. Together with the cumulative
    /// `credit_spent`/`credit_returned` counters this must satisfy `available == fetch_window -
    /// (spent - returned)` on every step: credit is either spendable or committed, never both
    /// (FLOW-2).
    std::uint32_t credit_available(const NodeId& peer) const;

private:
    struct WriterSlot {
        std::unique_ptr<LogWriter> log;
        std::uint64_t last_head_ms = 0;
    };
    /// What a peer has cost us so far. Kept inside PeerLink rather than in a second map: one
    /// lifetime rule, one expiry sweep, and nothing to prune when a peer leaves (SEC-2).
    struct PeerScore {
        std::uint32_t strikes = 0;
        /// When strikes were last reduced. Elapsed time accumulates against this mark, so a caller
        /// whose every step is too short to decay one strike still decays eventually.
        std::uint64_t decay_mark_ms = 0;
        std::uint32_t ban_count = 0;
        std::uint64_t banned_until_ms = 0;  // 0 means not banned
        /// A banned peer that completed a handshake is allowed one record to prove itself. That
        /// is the whole of "lifted early" - see docs/design/07-security.md.
        bool probation = false;
    };
    struct PeerLink {
        std::uint64_t last_hello_ms = 0;
        bool hello_sent = false;
        bool version_known = false;
        std::uint8_t version = 0;
        /// What the peer last advertised. Replaced wholesale, never merged: a peer that unfollows
        /// must be able to say so. Stale adverts are treated as unknown, so a worker that died last
        /// week stops being ranked.
        std::vector<std::string> patterns;
        std::uint8_t spare_capacity = 0;
        std::uint64_t advertised_ms = 0;
        /// Separate from the timestamp because a hello can legitimately arrive at time zero:
        /// without this, "advertised at 0" and "never advertised" would be the same state.
        bool advertised = false;
        PeerScore score;
    };
    /// What gossip knows about one peer. `hops` is distance from the origin (D4), never a
    /// countdown; `refreshed_ms` is the last time something confirmed the entry, which is the age
    /// it passes on - staleness, not longevity. A node that has known a peer for an hour still has
    /// a fresh entry.
    struct GossipEntry {
        GossipPeer peer;
        std::uint32_t hops = 0;
        std::uint64_t refreshed_ms = 0;
        std::uint64_t refreshed_seq = 0;  // tie-free recency, for eviction and "freshest first"
        /// Separate from the timestamp because an entry can legitimately be learned at time zero;
        /// otherwise "held at 0" and "never held" would be the same state.
        bool refreshed = false;
        bool offered = false;  // already handed to the caller to dial
    };
    struct FetchState {
        std::uint64_t sent_ms = 0;
        Seq from = 0;                   // first seq requested
        Seq end_seq = 0;                // last seq requested
        std::uint32_t credit_left = 0;  // credit this request has not accounted for yet
    };

    void handle(const NodeId& from, const Message& message, std::uint64_t now_ms);
    /// The single inbound choke point (SEC-2). False means "drop this message": the sender is
    /// banned and the message is not part of the exchange that can end the ban. Every inbound path
    /// goes through here, so a ban cannot be bypassed by inventing a message kind.
    bool admit(const NodeId& from, const Message& message, std::uint64_t now_ms);
    /// Record a fault against a peer, decay first, and ban it if that crosses the threshold. Always
    /// counted; it never changes what the data layer accepts (see the rule in 07-security.md).
    void penalize(const NodeId& peer, PeerFault fault, std::uint64_t now_ms);
    /// A verified record from a peer on probation: the ban is over.
    void confirmed(const NodeId& peer, std::uint64_t now_ms);
    void start_ban(const NodeId& peer, PeerLink& link, std::uint64_t now_ms);
    void lift_ban(const NodeId& peer, PeerLink& link, BanEdge edge, std::uint64_t now_ms);
    /// Clear every ban whose deadline has passed. Called from tick() so the lapse is reported even
    /// for a peer that has gone quiet.
    void expire_bans(std::uint64_t now_ms);
    void notify_ban(const BanEvent& event) const;
    void handle_record(const NodeId& from, const Record& record, bool direct, std::uint64_t now_ms);
    /// The per-record checks that come before the log sees a record: version, credit, whether we
    /// follow the channel, size, the channel rule, and the accounting for a repair reply. False
    /// means the record is refused; true means it is fit to ingest.
    bool precheck_record(const NodeId& from, const Record& record, bool direct,
                         std::uint64_t now_ms);
    /// What a node does with an ingest outcome: counters, callbacks, repair. One place, because
    /// both the single-record path and the batch path end here (LOG-2).
    void apply_ingest(const NodeId& from, const Record& record, bool direct, IngestResult result,
                      std::uint64_t now_ms);
    /// Ingest the collected repair run with one store call, then account for each outcome in order.
    void flush_direct_batch(std::uint64_t now_ms);
    void handle_head(const NodeId& from, const HeadMsg& head, std::uint64_t now_ms);
    void handle_fetch(const NodeId& from, const FetchMsg& fetch, std::uint64_t now_ms);
    void handle_ack(const NodeId& from, const AckMsg& ack);
    void handle_get_heads(const NodeId& from, const GetHeadsMsg& request, std::uint64_t now_ms);
    void handle_direct_head(const NodeId& from, const DirectHeadMsg& head, std::uint64_t now_ms);
    void handle_credit(const NodeId& from, const CreditMsg& credit);
    /// Merge a peer list from `from`. Refused whole when it is malformed or over the per-message
    /// cap; individual entries are refused for being stale, too far, or about ourselves (DISC-3).
    void handle_peers(const NodeId& from, const PeersMsg& message, std::uint64_t now_ms);
    /// The hash a checkpoint says the record at `seq` has, when a checkpoint covers it. This is how
    /// a truncated log keeps verifying.
    std::optional<Hash> checkpoint_hash(const LogKey& key, Seq seq) const;
    /// Offer this node's freshest entries to a rotating subset of neighbours, and expire what
    /// nothing refreshed. Called from tick().
    void gossip_tick(std::uint64_t now_ms);
    /// Per-peer budget: true when this peer may have another request of this kind right now.
    bool admit_request(const NodeId& peer, std::uint64_t now_ms);
    /// Does a channel rule permit this writer? Counts and names the rule when it does not, and
    /// remembers the log so we stop asking for records we would refuse.
    bool authorized(const LogKey& key);
    /// Announce what we hold for the patterns we follow, so a newcomer can find logs without the
    /// writer.
    void request_heads(std::uint64_t now_ms);
    /// Send an acknowledgement for logs we hold, rate-limited per peer.
    void acknowledge(std::uint64_t now_ms);
    /// Advance the retention floor for a log if enough replicas have acknowledged it.
    void advance_retention(const LogKey& key);
    /// How many sequences behind the highest head we know of for this log.
    Seq behind_by(const LogKey& key) const;
    void handle_hello(const NodeId& from, const HelloMsg& hello, std::uint64_t now_ms);
    /// True when this peer's advertisement is too old to use (and counted when it is read).
    bool advert_stale(const PeerLink& link, std::uint64_t now_ms) const;
    /// Rebuild the hello this node sends: its follow set, deduplicated and bounded, plus capacity.
    void refresh_advert(std::uint64_t now_ms);
    /// Send our version to any peer we have not confirmed a version for yet. Called from tick().
    void greet_peers(std::uint64_t now_ms);
    /// Is payload from this peer allowed? False (and counted) when it announced a version we cannot
    /// speak.
    bool accept_payload(const NodeId& peer);
    void request_fetch(const LogKey& key, const NodeId& peer, std::uint64_t now_ms);
    /// Return one credit for a repair record that answers a live request. A record belonging to a
    /// request that already ended (late response) is consumed and must not create credit (FLOW-2).
    /// Returns whether a live request accounted for this record at all.
    bool consume_credit(const NodeId& peer, const LogKey& key, Seq seq);
    void return_credit(const NodeId& peer, std::uint32_t amount);
    Seq next_seq(const LogKey& key) const;
    CreditWindow& credits_for(const NodeId& peer);

    NodeConfig config_;
    Transport& transport_;
    LogStore& store_;
    const Hasher& hasher_;
    ReplicaLog replica_;
    /// Following a trusted writer's rotation chain is how a rule that names a *root* stays honest
    /// after that writer rotates. Cached per (root, channel), invalidated when a link arrives.
    RotationChain rotation_;
    Matcher following_;

    std::map<LogKey, WriterSlot> writers_;
    std::map<LogKey, Seq> remote_head_;  // highest seq we have heard of for each log
    std::map<std::pair<LogKey, NodeId>, FetchState> fetches_;
    /// Highest sequence we ever asked this peer for, per log. It is what separates an unsolicited
    /// repair record (an objective fault) from a late or duplicated answer to a request we did
    /// make - and loss and duplication must never earn a strike.
    std::map<std::pair<LogKey, NodeId>, Seq> requested_through_;
    std::map<NodeId, CreditWindow> peer_credits_;
    std::map<NodeId, PeerLink> peer_links_;
    std::map<std::pair<LogKey, NodeId>, Seq>
        acked_;  // highest sequence each peer confirmed per log
    std::map<LogKey, Seq>
        released_through_;  // retention floor: records at or below this are released
    std::map<LogKey, std::uint64_t> shortfall_at_;  // when a log last hit the retention limit
    std::map<NodeId, CreditWindow> live_credits_;   // live-path credit, opt-in per peer
    std::map<NodeId, bool> live_credit_enabled_;
    std::map<LogKey, std::uint64_t> last_ack_ms_;
    /// Logs we have accepted records for. Acknowledgements iterate this set, never the store's
    /// inventory: asking the store for its keys is a full scan in the durable backend and would run
    /// every tick.
    std::set<LogKey> ingested_logs_;
    std::map<LogKey, NodeId>
        source_peer_;  // where a log's records actually came from (acks go there)
    std::map<NodeId, std::uint64_t> last_heads_request_ms_;
    std::vector<std::string> patterns_;  // what we follow, replayed to peers when asking for heads
    const ChannelRules* rules_ = nullptr;  // null = open, the default
    std::map<std::string, std::uint64_t> unauthorized_by_pattern_;
    /// Logs a rule forbids: without this, a refused writer's head announcements would make us fetch
    /// records we will refuse forever - a traffic amplifier handed to whoever can publish a head.
    std::set<LogKey> unauthorized_logs_;
    std::map<NodeId, std::pair<std::uint64_t, std::uint32_t>>
        request_budget_;                    // peer -> (window start, used)
    std::map<NodeId, GossipEntry> gossip_;  // peers heard about but not necessarily reachable
    std::set<NodeId> configured_peers_;     // addresses that are not gossip's to change
    std::size_t gossip_cursor_ = 0;         // rotates the neighbours a round is offered to
    std::uint64_t last_gossip_ms_ = 0;
    std::uint64_t gossip_seq_ = 0;      // increments per accepted entry, so recency never ties
    std::size_t gossip_rotate_ = 0;     // rotates the window of entries one message can carry
    std::uint64_t last_advert_ms_ = 0;  // when the outgoing hello was last rebuilt
    bool advert_refreshed_ = false;     // so "never built" is not confused with "built at 0"
    std::uint64_t last_retention_ms_ = 0;
    /// A repair reply arrives as a run of DirectRecordMsg from one peer for one log. Collecting the
    /// run and appending it once is what makes a 256-record reply cost one store call (LOG-2).
    std::optional<std::pair<NodeId, LogKey>> direct_batch_key_;
    std::vector<Record> direct_batch_;
    std::vector<std::string> advert_patterns_;  // what we tell peers we follow
    std::uint8_t spare_capacity_ = 0;           // what we tell peers about our backlog
    /// The last time the caller told us what time it is: staleness is computed against this,
    /// because the node never reads a clock.
    std::uint64_t last_now_ms_ = 0;
    NodeCounters counters_;
    RecordHandler on_record_;
    RecordHandler on_fork_;
    std::function<void(const BanEvent&)> on_ban_;
};

}  // namespace mosaic
