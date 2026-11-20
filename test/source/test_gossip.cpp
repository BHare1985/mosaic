// Peer-list gossip: discovery for networks the beacon cannot reach, under the rule that learning an
// address grants nothing (spec: discovery/peer-discovery, DISC-3).
#include <mosaic/core/insecure_test_signer.h>
#include <mosaic/net/sim_network.h>
#include <mosaic/node/node.h>
#include <mosaic/proto/codec.h>
#include <mosaic/store/memory_log_store.h>

#include <doctest/doctest.h>
#include <string>
#include <vector>

using std::initializer_list;
using std::size_t;
using std::string;
using std::uint64_t;
using std::vector;

using namespace mosaic;

namespace {
Bytes text(const string& s) {
    return Bytes(s.begin(), s.end());
}

/// A node with addresses, so it can introduce itself: gossip is useless if a node only ever repeats
/// what a third party said about it.
struct Peer {
    SimTransport transport;
    MemoryLogStore store;
    Node node;
    vector<string> received;

    Peer(SimNetwork& net, const NodeId& id, const Hasher& hasher, const Verifier& verifier,
         const NodeConfig& base = {})
        : transport(net, id), node(config_for(base, id), transport, store, hasher, verifier) {
        node.on_record(
            [this](const Record& r) { received.emplace_back(r.payload.begin(), r.payload.end()); });
    }

    static NodeConfig config_for(NodeConfig config, const NodeId& id) {
        config.id = id;
        config.pub_endpoint = "tcp://" + id + ":6200";
        config.router_endpoint = "tcp://" + id + ":6201";
        return config;
    }

    bool knows(const NodeId& id) const {
        for (const auto& peer : node.gossip_peers())
            if (peer.id == id)
                return true;
        return false;
    }
};

void step(SimNetwork& net, uint64_t now_ms, initializer_list<Peer*> peers) {
    net.advance(now_ms);
    for (Peer* peer : peers) {
        peer->node.pump(now_ms);
        peer->node.tick(now_ms);
    }
}

PeerEntry entry_for(const NodeId& id, const string& host, uint32_t hops = 1, uint64_t age_ms = 0) {
    return PeerEntry{id, "tcp://" + host + ":6200", "tcp://" + host + ":6201", hops, age_ms};
}

NodeConfig gossip_config() {
    NodeConfig config;
    config.gossip_interval_ms = 100;
    config.gossip_fanout = 2;
    config.gossip_max_hops = 6;
    config.gossip_entry_ttl_ms = 1000;
    config.gossip_table_limit = 16;
    config.control_interval_ms = 50;
    return config;
}
}  // namespace

TEST_CASE("gossip carries a peer three hops away") {
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(301);
    const auto config = gossip_config();

    // A line: a-b-c-d. Nothing can reach the far end except by being told about it.
    Peer a(net, "a", hasher, alice, config), b(net, "b", hasher, alice, config),
        c(net, "c", hasher, alice, config), d(net, "d", hasher, alice, config);
    net.partition("a", "c");
    net.partition("a", "d");
    net.partition("b", "d");

    uint64_t now = 0;
    for (int i = 0; i < 40; ++i) {
        now += 100;
        step(net, now, {&a, &b, &c, &d});
    }

    CHECK(d.knows("a"));  // learned through b and c, three hops from its origin
    CHECK(a.knows("d"));
    CHECK(b.knows("d"));
    CHECK(d.node.counters().peers_learned > 0);
    CHECK(d.node.counters().peers_gossiped > 0);
}

TEST_CASE("an entry nothing refreshes expires") {
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(303);
    NodeConfig config = gossip_config();
    config.gossip_interval_ms = 0;  // this node gossips nothing; it only receives

    Peer node(net, "node", hasher, alice, config);
    Peer sender(net, "sender", hasher, alice, config);

    sender.transport.send("node", PeersMsg{{entry_for("far", "10.0.0.9")}});
    step(net, 0, {&node});
    REQUIRE(node.knows("far"));
    CHECK(node.node.counters().peers_learned == 1);
    const auto dialable = node.node.take_new_gossip_peers();
    REQUIRE(dialable.size() == 1);
    CHECK(dialable.front().id == "far");

    // Past the lifetime, with nobody refreshing it: gone, and not offered again.
    step(net, config.gossip_entry_ttl_ms + 1, {&node});
    CHECK_FALSE(node.knows("far"));
    CHECK(node.node.counters().peers_expired == 1);
    CHECK(node.node.take_new_gossip_peers().empty());
}

TEST_CASE("a gossiped address cannot move a peer the transport already has") {
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(305);
    NodeConfig config = gossip_config();
    config.gossip_interval_ms = 0;

    Peer node(net, "node", hasher, alice, config);
    Peer known(net, "known", hasher, alice, config);
    Peer liar(net, "liar", hasher, alice, config);
    node.node.set_configured_peers({"known"});  // placed by configuration, not by a hint

    // The entry claims "known" lives somewhere else entirely.
    liar.transport.send("node", PeersMsg{{entry_for("known", "203.0.113.7")}});
    step(net, 0, {&node});

    // It is never handed to the dialler, and the claimed address never enters the table at all.
    CHECK(node.node.take_new_gossip_peers().empty());
    for (const auto& peer : node.node.gossip_peers())
        CHECK(peer.pub_endpoint.find("203.0.113.7") == string::npos);
    CHECK(node.node.counters().peers_learned == 0);
}

TEST_CASE("losing the introducer does not lose the peer") {
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(307);
    const auto config = gossip_config();

    Peer a(net, "a", hasher, alice, config), b(net, "b", hasher, alice, config),
        c(net, "c", hasher, alice, config);
    c.node.follow("events.>");

    uint64_t now = 0;
    for (int i = 0; i < 10; ++i) {
        now += 100;
        step(net, now, {&a, &b, &c});
    }
    REQUIRE(c.knows("b"));

    // A goes away. B keeps advertising itself, so C's knowledge of it survives on its own.
    for (int i = 0; i < 30; ++i) {
        now += 100;
        step(net, now, {&b, &c});
    }
    CHECK(c.knows("b"));

    // And the link it already had still replicates: an introducer is not a dependency.
    const auto record = b.node.publish(alice, "events.a", "t", text("after-a-left"), now);
    REQUIRE(record.has_value());
    for (int i = 0; i < 10; ++i) {
        now += 100;
        step(net, now, {&b, &c});
    }
    REQUIRE(c.received.size() == 1);
    CHECK(c.received.front() == "after-a-left");
}

TEST_CASE("an oversized peer list is refused whole, counted and scored") {
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(311);
    NodeConfig config = gossip_config();
    config.gossip_interval_ms = 0;
    config.strike_threshold = 100;  // count the strike without banning, so the score is readable

    Peer node(net, "node", hasher, alice, config);
    Peer noisy(net, "noisy", hasher, alice, config);

    PeersMsg flood;
    for (uint32_t i = 0; i < kMaxPeersEntries + 1; ++i)
        flood.entries.push_back(entry_for("p" + std::to_string(i), "10.0.0.1"));
    noisy.transport.send("node", flood);
    step(net, 0, {&node});

    CHECK(node.node.gossip_peers().empty());  // nothing from a message we could not use
    CHECK(node.node.counters().gossip_refused == 1);
    CHECK(node.node.peer_strikes("noisy", 0) >= strike_weight(PeerFault::Undecodable));

    // A list that fits the message still cannot grow the table past its bound.
    NodeConfig small = config;
    small.gossip_table_limit = 4;
    Peer bounded(net, "bounded", hasher, alice, small);
    noisy.transport.send("bounded", PeersMsg{[&] {
                             vector<PeerEntry> entries;
                             for (uint32_t i = 0; i < kMaxPeersEntries; ++i)
                                 entries.push_back(entry_for("q" + std::to_string(i), "10.0.0.1"));
                             return entries;
                         }()});
    step(net, 10, {&bounded});
    CHECK(bounded.node.gossip_peers().size() <= 4);
    CHECK(bounded.node.counters().gossip_overflow > 0);
}

TEST_CASE("entries are refused for being stale, too far, or about ourselves") {
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(313);
    NodeConfig config = gossip_config();
    config.gossip_interval_ms = 0;
    config.gossip_entry_ttl_ms = 1000;
    config.gossip_max_hops = 6;

    Peer node(net, "node", hasher, alice, config);
    Peer sender(net, "sender", hasher, alice, config);

    PeersMsg mixed{{
        entry_for("stale", "10.0.0.2", 1, 5000),  // older than the ttl
        entry_for("distant", "10.0.0.3", config.gossip_max_hops,
                  0),                    // would arrive one hop too far
        entry_for("node", "10.0.0.4"),   // ourselves
        entry_for("fresh", "10.0.0.5"),  // the only one worth keeping
    }};
    sender.transport.send("node", mixed);
    step(net, 0, {&node});

    CHECK(node.node.counters().gossip_refused == 3);
    CHECK(node.node.counters().peers_learned == 1);
    REQUIRE(node.node.gossip_peers().size() == 1);
    CHECK(node.node.gossip_peers().front().id == "fresh");
}

TEST_CASE("a gossip storm is throttled by the per-peer budget") {
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(317);
    NodeConfig config = gossip_config();
    config.gossip_interval_ms = 0;
    config.max_requests_per_interval = 3;
    config.control_interval_ms = 1000;

    Peer node(net, "node", hasher, alice, config);
    Peer storm(net, "storm", hasher, alice, config);

    for (int i = 0; i < 20; ++i)
        storm.transport.send("node", PeersMsg{{entry_for("p" + std::to_string(i), "10.0.0.1")}});
    step(net, 0, {&node});

    CHECK(node.node.gossip_peers().size() <= config.max_requests_per_interval);
    CHECK(node.node.counters().requests_throttled > 0);
    CHECK(node.node.counters().peers_learned == config.max_requests_per_interval);
}
