#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <mosaic/match/matcher.h>
#include <mosaic/net/transport.h>
#include <random>
#include <set>
#include <utility>

namespace mosaic {

class SimTransport;

/// Fault knobs for one direction of one link. Loss and duplication are probabilities in [0,1]. Delay
/// is a *range*: a constant delay cannot reorder, and reordering is exactly what a follower must
/// survive, so jitter is the mechanism that produces it (SIM-1).
struct LinkConfig {
    double loss = 0.0;
    double duplicate = 0.0;
    std::uint32_t delay_min_ms = 0;
    std::uint32_t delay_max_ms = 0;
};

/// Deterministic in-process network for tests, and the only evidence for the system-level
/// requirements (`openspec/specs/harness/simulation/spec.md`).
///
/// Delivery is *scheduled*, not immediate: a send picks an arrival time, and `advance(now_ms)` releases
/// what is due. That makes latency, reordering, timeouts and partitions expressible in simulated time,
/// with no sleeping and no wall clock.
///
/// Determinism: every link direction owns a generator stream derived from `(seed, from, to)`, so adding
/// a peer does not change the faults an existing link suffers, and a reported seed reproduces a failure
/// exactly. Time is owned by the caller; the network reads no clock.
class SimNetwork {
public:
    explicit SimNetwork(std::uint64_t seed = 1);

    /// Apply to every link, existing and future.
    void set_loss(double probability);
    void set_duplicate(double probability);
    void set_delay(std::uint32_t min_ms, std::uint32_t max_ms);

    /// Override the faults of one direction (from -> to).
    void set_link(const NodeId& from, const NodeId& to, LinkConfig config);

    /// Break connectivity in both directions; heal() restores it. Sends into a partition are dropped
    /// silently, exactly like loss, so a node cannot tell the difference.
    void partition(const NodeId& a, const NodeId& b);
    void heal(const NodeId& a, const NodeId& b);
    bool partitioned(const NodeId& a, const NodeId& b) const;

    /// Set the simulated clock and release every message whose arrival time has come. Sends issued
    /// from now on are scheduled relative to this time.
    void advance(std::uint64_t now_ms);

    std::uint64_t now() const { return now_ms_; }
    std::size_t delivered() const { return delivered_; }
    std::size_t dropped() const { return dropped_; }
    std::size_t duplicated() const { return duplicated_; }
    /// Deliveries that arrived out of send order; a test asserts this actually happens rather than
    /// assuming the jitter did something.
    std::size_t reordered() const { return reordered_; }

private:
    friend class SimTransport;

    struct Peer {
        std::deque<Envelope> inbox;
        Matcher subs;
    };
    struct LinkState {
        LinkConfig config;
        std::mt19937_64 rng;
        std::uint64_t sent = 0;
        std::uint64_t last_delivered = 0;
    };
    // Deliberately holds a Message rather than an Envelope: nesting a variant inside an aggregate that
    // is then moved as a whole trips a GCC 15 maybe-uninitialized false positive under -O2 -Werror.
    struct Pending {
        NodeId from;
        NodeId to;
        std::uint64_t seq = 0;
        Message message;
    };

    void add_peer(const NodeId& id);
    void schedule(const NodeId& from, const NodeId& to, const Message& message);
    LinkState& link(const NodeId& from, const NodeId& to);

    std::map<NodeId, Peer> peers_;
    std::map<std::pair<NodeId, NodeId>, LinkState> links_;
    std::set<std::pair<NodeId, NodeId>> blocked_; // ordered pairs: partition inserts both directions
    std::multimap<std::pair<std::uint64_t, std::uint64_t>, Pending> pending_; // (due_ms, seq)

    std::uint64_t seed_;
    std::uint64_t now_ms_ = 0;
    LinkConfig defaults_;
    std::size_t delivered_ = 0;
    std::size_t dropped_ = 0;
    std::size_t duplicated_ = 0;
    std::size_t reordered_ = 0;
};

/// One node's endpoint on a SimNetwork. The network must outlive it.
class SimTransport final : public Transport {
public:
    SimTransport(SimNetwork& net, NodeId id);

    NodeId self() const override { return id_; }
    void subscribe(const std::string& pattern) override;
    void publish(const Channel& channel, Message message) override;
    void send(const NodeId& to, Message message) override;
    std::optional<Envelope> poll() override;

private:
    SimNetwork& net_;
    NodeId id_;
};

} // namespace mosaic
