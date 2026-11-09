#pragma once

#include <deque>
#include <map>
#include <mosaic/match/matcher.h>
#include <mosaic/net/transport.h>
#include <random>

namespace mosaic {

class SimTransport;

/// Deterministic in-process network for tests: every node can reach every other node; messages can be
/// dropped or duplicated using a seeded RNG, so a failing run is reproducible from its seed.
/// Delivery is immediate (no latency/reorder yet: SIM-1). Time is owned by the test, not the network.
class SimNetwork {
public:
    explicit SimNetwork(std::uint64_t seed = 1)
        : rng_(seed)
    {
    }

    void set_loss(double probability) { loss_ = probability; }
    void set_duplicate(double probability) { duplicate_ = probability; }

    std::size_t delivered() const { return delivered_; }
    std::size_t dropped() const { return dropped_; }

private:
    friend class SimTransport;
    struct Peer {
        std::deque<Envelope> inbox;
        Matcher subs;
    };

    void add_peer(const NodeId& id) { peers_[id]; }
    void deliver(const NodeId& from, const NodeId& to, const Message& message);

    std::map<NodeId, Peer> peers_;
    std::mt19937_64 rng_;
    double loss_ = 0;
    double duplicate_ = 0;
    std::size_t delivered_ = 0;
    std::size_t dropped_ = 0;
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
