#include <doctest/doctest.h>
#include <mosaic/net/sim_network.h>
#include <mosaic/node/node.h>
#include <mosaic/store/memory_log_store.h>

using namespace mosaic;

namespace {
Bytes bytes(const std::string& s) { return Bytes(s.begin(), s.end()); }

struct Peer {
    SimTransport transport;
    MemoryLogStore store;
    Node node;
    std::vector<std::string> received;
    Peer(SimNetwork& net, const NodeId& id, const Hasher& h, const Verifier& v)
        : transport(net, id)
        , node({ .id = id }, transport, store, h, v)
    {
        node.on_record([this](const Record& r) { received.emplace_back(r.payload.begin(), r.payload.end()); });
    }
};
}

TEST_CASE("lossless: followers receive live records in order; non-followers do not")
{
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(1);
    Peer writer(net, "writer", hasher, alice), follower(net, "follower", hasher, alice), other(net, "other", hasher, alice);
    follower.node.follow("events.>");
    other.node.follow("unrelated.>");

    for (int i = 0; i < 5; ++i)
        writer.node.publish(alice, "events.a", "t", bytes("m" + std::to_string(i)), 0);
    follower.node.pump(0);
    other.node.pump(0);

    CHECK(follower.received == std::vector<std::string> { "m0", "m1", "m2", "m3", "m4" });
    CHECK(other.received.empty());
    CHECK_FALSE(other.node.head({ "alice", "events.a" }).has_value());
}

TEST_CASE("a late joiner catches up through HEAD + FETCH")
{
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(1);
    Peer writer(net, "writer", hasher, alice);
    for (int i = 0; i < 200; ++i) // more than one fetch_batch (64)
        writer.node.publish(alice, "events.a", "t", bytes(std::to_string(i)), 0);

    Peer late(net, "late", hasher, alice);
    late.node.follow("events.>");

    std::uint64_t now = 0;
    for (int step = 0; step < 100 && late.received.size() < 200; ++step) {
        now += 100;
        writer.node.tick(now);
        writer.node.pump(now);
        late.node.pump(now);
        late.node.tick(now);
    }
    REQUIRE(late.received.size() == 200);
    for (int i = 0; i < 200; ++i)
        CHECK(late.received[static_cast<std::size_t>(i)] == std::to_string(i));
}

// The headline property from docs/design/08-testing-strategy.md: whatever the network does, every
// follower ends up with a log identical to the writer's.
TEST_CASE("converges under 30% loss and 10% duplication")
{
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(7);
    net.set_loss(0.3);
    net.set_duplicate(0.1);

    Peer writer(net, "writer", hasher, alice), r1(net, "r1", hasher, alice), r2(net, "r2", hasher, alice);
    r1.node.follow("events.>");
    r2.node.follow("events.a");

    std::uint64_t now = 0;
    int published = 0;
    const int total = 150;
    bool converged = false;
    for (int step = 0; step < 4000 && !converged; ++step) {
        now += 50;
        if (published < total) {
            writer.node.publish(alice, published % 2 ? "events.a" : "events.b", "t", bytes(std::to_string(published)), now);
            ++published;
        }
        for (Peer* p : { &writer, &r1, &r2 }) {
            p->node.pump(now);
            p->node.tick(now);
        }
        if (published == total) {
            const auto wa = writer.node.head({ "alice", "events.a" });
            const auto wb = writer.node.head({ "alice", "events.b" });
            converged = r1.node.head({ "alice", "events.a" }) && r1.node.head({ "alice", "events.b" })
                && r2.node.head({ "alice", "events.a" })
                && r1.node.head({ "alice", "events.a" })->hash == wa->hash
                && r1.node.head({ "alice", "events.b" })->hash == wb->hash
                && r2.node.head({ "alice", "events.a" })->hash == wa->hash;
        }
    }
    CHECK(converged);
    CHECK(net.dropped() > 0);
    CHECK_FALSE(r2.node.head({ "alice", "events.b" }).has_value()); // r2 only follows events.a
    CHECK(r1.received.size() == static_cast<std::size_t>(total));
    CHECK(r2.received.size() == static_cast<std::size_t>(total / 2));
}

TEST_CASE("forged records are rejected and reported")
{
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(1);
    Peer follower(net, "follower", hasher, alice);
    follower.node.follow("events.>");
    SimTransport evil(net, "evil");

    Record forged;
    forged.writer = "alice";
    forged.channel = "events.a";
    forged.payload = bytes("fake");
    forged.sig = bytes("not a signature");
    evil.send("follower", RecordMsg { forged });
    follower.node.pump(0);

    CHECK(follower.received.empty());
    CHECK_FALSE(follower.node.head({ "alice", "events.a" }).has_value());
}
