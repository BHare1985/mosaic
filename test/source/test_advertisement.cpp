// What peers tell each other about themselves: what they follow, and how much spare work they have.
// Every use of it must be safe when the advertisement is wrong, stale or hostile
// (spec: proto/peer-advertisement, HELLO-1).
#include <mosaic/core/insecure_test_signer.h>
#include <mosaic/net/sim_network.h>
#include <mosaic/node/node.h>
#include <mosaic/store/memory_log_store.h>

#include <doctest/doctest.h>
#include <string>
#include <vector>

using std::initializer_list;
using std::string;
using std::uint64_t;
using std::vector;

using namespace mosaic;

namespace {
Bytes text(const string& s) {
    return Bytes(s.begin(), s.end());
}

struct Peer {
    SimTransport transport;
    MemoryLogStore store;
    Node node;
    Peer(SimNetwork& net, const NodeId& id, const Hasher& hasher, const Verifier& verifier,
         const NodeConfig& base = {})
        : transport(net, id), node(config_for(base, id), transport, store, hasher, verifier) {}

    static NodeConfig config_for(NodeConfig config, const NodeId& id) {
        config.id = id;
        return config;
    }
};

void step(SimNetwork& net, uint64_t now_ms, initializer_list<Peer*> peers) {
    net.advance(now_ms);
    for (Peer* peer : peers) {
        peer->node.pump(now_ms);
        peer->node.tick(now_ms);
    }
}

bool contains(const vector<string>& values, const string& wanted) {
    for (const auto& value : values)
        if (value == wanted)
            return true;
    return false;
}

NodeConfig advert_config() {
    NodeConfig config;
    config.head_interval_ms = 100;  // advertise often enough for a test to see it
    return config;
}
}  // namespace

TEST_CASE("a peer's hello carries what it follows") {
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(501);

    Peer watcher(net, "watcher", hasher, alice, advert_config());
    Peer writer(net, "writer", hasher, alice, advert_config());
    writer.node.follow("events.>");
    writer.node.follow("svc.compress");

    uint64_t now = 0;
    for (int i = 0; i < 10; ++i) {
        now += 100;
        step(net, now, {&watcher, &writer});
    }

    const auto patterns = watcher.node.peer_patterns("writer");
    CHECK(contains(patterns, "events.>"));
    CHECK(contains(patterns, "svc.compress"));
    CHECK(watcher.node.peers_following("events.a") == vector<NodeId>{"writer"});
    CHECK(watcher.node.peers_following("svc.compress") == vector<NodeId>{"writer"});
    CHECK(watcher.node.peers_following("other.log").empty());
    CHECK(watcher.node.peer_patterns("nobody").empty());  // an unknown peer advertises nothing
}

TEST_CASE("a later hello replaces the earlier set, and an oversized one is truncated") {
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(503);

    NodeConfig config = advert_config();
    config.max_head_patterns = 4;
    Peer node(net, "node", hasher, alice, config);
    Peer peer(net, "peer", hasher, alice, config);

    peer.transport.send("node", HelloMsg{kProtocolVersion, {"events.>"}, 0});
    step(net, 0, {&node});
    CHECK(node.node.peer_patterns("peer") == vector<string>{"events.>"});

    // A second hello replaces rather than accumulates: unfollowing has to be expressible.
    peer.transport.send("node", HelloMsg{kProtocolVersion, {"storage.>"}, 0});
    step(net, 10, {&node});
    CHECK(node.node.peer_patterns("peer") == vector<string>{"storage.>"});

    // More patterns than the limit: kept up to the limit, and the truncation is counted rather than
    // the whole hello being refused.
    HelloMsg flood;
    for (uint32_t i = 0; i < config.max_head_patterns + 3; ++i)
        flood.patterns.push_back("p" + std::to_string(i));
    peer.transport.send("node", flood);
    step(net, 20, {&node});
    CHECK(node.node.peer_patterns("peer").size() == config.max_head_patterns);
    CHECK(node.node.counters().patterns_truncated == 1);
}

TEST_CASE("capacity is advertised, and stale advertisements stop being believed") {
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(505);

    NodeConfig config = advert_config();
    Peer node(net, "node", hasher, alice, config);
    Peer peer(net, "peer", hasher, alice, config);

    peer.transport.send("node", HelloMsg{kProtocolVersion, {"svc.compress"}, 42});
    step(net, 0, {&node});
    REQUIRE(node.node.peer_capacity("peer").has_value());
    CHECK(*node.node.peer_capacity("peer") == 42);
    CHECK(node.node.counters().capacity_unknown == 0);

    // Nobody sends another hello: after several hello intervals the figure is unknown, not spare.
    const auto stale_at = config.head_interval_ms * kHelloStaleIntervals + 1;
    step(net, stale_at, {&node});
    CHECK_FALSE(node.node.peer_capacity("peer").has_value());
    CHECK(node.node.peers_following("svc.compress").empty());
    CHECK(node.node.counters().capacity_unknown > 0);
}

TEST_CASE("an advertisement grants nothing: delivery still follows matching, not the hello") {
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(507);

    Peer writer(net, "writer", hasher, alice, advert_config());
    Peer follower(net, "follower", hasher, alice, advert_config());
    follower.node.follow("events.>");

    // The follower advertises capacity but nothing about following; the writer's head is enough.
    follower.node.set_spare_capacity(255);
    const auto record = writer.node.publish(alice, "events.a", "t", text("still delivered"), 0);
    REQUIRE(record.has_value());

    uint64_t now = 0;
    for (int i = 0; i < 6; ++i) {
        now += 50;
        step(net, now, {&writer, &follower});
    }
    CHECK(follower.node.head({"alice", "events.a"}).has_value());
}
