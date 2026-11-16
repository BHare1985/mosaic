#include <doctest/doctest.h>
#include <mosaic/net/sim_network.h>
#include <mosaic/node/node.h>
#include <mosaic/store/memory_log_store.h>
#include <string>
#include <vector>

#include "scenario.h"

using namespace mosaic;
using mosaic::test::first_failing_seed;
using mosaic::test::seed_count;

namespace {
Bytes bytes(const std::string& s) { return Bytes(s.begin(), s.end()); }

struct Peer {
    SimTransport transport;
    MemoryLogStore store;
    Node node;
    std::vector<std::string> received;
    std::vector<std::string> forks;

    Peer(SimNetwork& net, const NodeId& id, const Hasher& hasher, const Verifier& verifier, const NodeConfig& config = { })
        : transport(net, id)
        , node(named(config, id), transport, store, hasher, verifier)
    {
        node.on_record([this](const Record& r) { received.emplace_back(r.payload.begin(), r.payload.end()); });
        node.on_fork([this](const Record& r) { forks.emplace_back(r.payload.begin(), r.payload.end()); });
    }

    static NodeConfig named(NodeConfig config, const NodeId& id)
    {
        config.id = id;
        return config;
    }
};

/// One simulated step: advance the clock (releasing everything due) and let each node work.
/// Time is injected everywhere, so a step costs no real time however long the outage it models.
void step(SimNetwork& net, std::uint64_t now_ms, std::initializer_list<Peer*> peers)
{
    net.advance(now_ms);
    for (Peer* peer : peers) {
        peer->node.pump(now_ms);
        peer->node.tick(now_ms);
    }
}

bool scenario_convergence(std::uint64_t seed)
{
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(seed);
    net.set_loss(0.3);
    net.set_duplicate(0.1);
    net.set_delay(0, 120); // jitter, so records arrive out of order as well as late

    Peer writer(net, "writer", hasher, alice), r1(net, "r1", hasher, alice), r2(net, "r2", hasher, alice);
    r1.node.follow("events.>");
    r2.node.follow("events.a");

    std::uint64_t now = 0;
    int published = 0;
    const int total = 100;
    bool converged = false;
    for (int step_count = 0; step_count < 8000 && !converged; ++step_count) {
        now += 50;
        if (published < total) {
            writer.node.publish(alice, published % 2 ? "events.a" : "events.b", "t", bytes(std::to_string(published)), now);
            ++published;
        }
        step(net, now, { &writer, &r1, &r2 });
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
    if (!converged)
        return false;
    return r1.received.size() == static_cast<std::size_t>(total)
        && r2.received.size() == static_cast<std::size_t>(total / 2)
        && net.dropped() > 0;
}
} // namespace

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
    step(net, 0, { &writer, &follower, &other });

    CHECK(follower.received == std::vector<std::string> { "m0", "m1", "m2", "m3", "m4" });
    CHECK(other.received.empty());
    CHECK_FALSE(other.node.head({ "alice", "events.a" }).has_value());

    // Counters make the same claim without reading log contents (NODE-1).
    CHECK(follower.node.counters().accepted == 5);
    CHECK(follower.node.counters().duplicate == 0);
    CHECK(follower.node.counters().bad_signature == 0);
    CHECK(other.node.counters().accepted == 0);
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
    for (int step_count = 0; step_count < 400 && late.received.size() < 200; ++step_count) {
        now += 50;
        step(net, now, { &writer, &late });
    }

    REQUIRE(late.received.size() == 200);
    for (int i = 0; i < 200; ++i)
        CHECK(late.received[static_cast<std::size_t>(i)] == std::to_string(i));
    CHECK(late.node.counters().fetches_sent > 0);
    CHECK(late.node.counters().accepted == 200);
    // Repair from HEAD fetches forward from seq 0, so this path never produces a gap; a gap needs a
    // *live* record that jumps ahead, which the loss scenarios below provide.
    CHECK(late.node.counters().gap == 0);
}

TEST_CASE("delay hides a message until its arrival time")
{
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(3);
    net.set_delay(100, 100);
    Peer writer(net, "writer", hasher, alice), follower(net, "follower", hasher, alice);
    follower.node.follow("events.>");

    writer.node.publish(alice, "events.a", "t", bytes("one"), 0);
    step(net, 0, { &writer, &follower });
    CHECK(follower.received.empty());
    step(net, 99, { &writer, &follower });
    CHECK(follower.received.empty());
    step(net, 100, { &writer, &follower });
    CHECK(follower.received.size() == 1);
}

TEST_CASE("jitter actually reorders deliveries")
{
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(5);
    net.set_delay(0, 50); // a range, so arrival order can differ from send order
    Peer writer(net, "writer", hasher, alice), follower(net, "follower", hasher, alice);
    follower.node.follow("events.>");

    for (int i = 0; i < 20; ++i)
        writer.node.publish(alice, "events.a", "t", bytes(std::to_string(i)), 0);
    net.advance(200);
    CHECK(net.reordered() > 0); // the fault model is real, not assumed

    // Arriving out of order means the follower sees holes and must repair them; it converges anyway.
    std::uint64_t now = 200;
    for (int step_count = 0; step_count < 400 && follower.received.size() < 20; ++step_count) {
        now += 50;
        step(net, now, { &writer, &follower });
    }
    CHECK(follower.received.size() == 20);
    CHECK(follower.node.counters().gap > 0);
}

TEST_CASE("a partition blocks traffic and healing restores convergence")
{
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(9);
    Peer writer(net, "writer", hasher, alice), follower(net, "follower", hasher, alice);
    follower.node.follow("events.>");

    net.partition("writer", "follower");
    for (int i = 0; i < 10; ++i)
        writer.node.publish(alice, "events.a", "t", bytes(std::to_string(i)), 0);
    step(net, 100, { &writer, &follower });
    CHECK(follower.received.empty());
    CHECK(net.dropped() > 0);

    net.heal("writer", "follower");
    std::uint64_t now = 100;
    for (int step_count = 0; step_count < 400 && follower.received.size() < 10; ++step_count) {
        now += 100;
        step(net, now, { &writer, &follower });
    }
    CHECK(follower.received.size() == 10);
}

TEST_CASE("forged records are rejected, counted and reported once")
{
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(11);
    Peer writer(net, "writer", hasher, alice), follower(net, "follower", hasher, alice);
    follower.node.follow("events.>");

    const auto genuine = writer.node.publish(alice, "events.a", "t", bytes("real"), 0);
    step(net, 0, { &writer, &follower });
    REQUIRE(follower.received.size() == 1);

    Record forged = genuine;
    forged.seq = 1;
    forged.prev = record_hash(genuine, hasher);
    forged.payload = bytes("fake"); // signature still covers the original content
    writer.transport.send("follower", RecordMsg { forged });
    step(net, 0, { &writer, &follower });

    CHECK(follower.node.counters().bad_signature == 1);
    CHECK(follower.received.size() == 1);
    CHECK(follower.node.head({ "alice", "events.a" })->seq == 0);
}

TEST_CASE("duplicated delivery is counted but reaches the app once")
{
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(13);
    net.set_duplicate(1.0); // every message delivered twice
    Peer writer(net, "writer", hasher, alice), follower(net, "follower", hasher, alice);
    follower.node.follow("events.>");

    for (int i = 0; i < 4; ++i)
        writer.node.publish(alice, "events.a", "t", bytes(std::to_string(i)), 0);
    step(net, 0, { &writer, &follower });

    CHECK(net.duplicated() >= 4);
    CHECK(follower.received.size() == 4);
    CHECK(follower.node.counters().duplicate == 4);
}

TEST_CASE("credit is accounted per request and is never inflated by late responses")
{
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(4);
    net.set_delay(150, 150); // every response arrives after the fetch timeout

    NodeConfig config;
    config.fetch_window = 4;
    config.fetch_batch = 2;
    config.fetch_timeout_ms = 100;
    config.head_interval_ms = 50;

    Peer writer(net, "writer", hasher, alice), follower(net, "follower", hasher, alice, config);
    for (int i = 0; i < 6; ++i) // published before anyone follows, so everything comes from repair
        writer.node.publish(alice, "events.a", "t", bytes(std::to_string(i)), 0);
    follower.node.follow("events.>");

    std::uint64_t now = 0;
    std::uint64_t max_outstanding = 0;
    for (int step_count = 0; step_count < 400 && follower.received.size() < 6; ++step_count) {
        now += 10;
        step(net, now, { &writer, &follower });

        // Measured from cumulative accounting, not from the live-request table: a blanket reset()
        // empties that table while the old round's responses are still in flight, which is exactly the
        // state that must never be reachable.
        const auto spent = follower.node.counters().credit_spent;
        const auto returned = follower.node.counters().credit_returned;
        REQUIRE(spent >= returned);
        const auto outstanding = spent - returned;
        REQUIRE(outstanding <= config.fetch_window);
        REQUIRE(follower.node.credit_available("writer") == config.fetch_window - outstanding);
        max_outstanding = outstanding > max_outstanding ? outstanding : max_outstanding;
    }

    CHECK(follower.received.size() == 6);
    CHECK(max_outstanding > 0); // the window really was used, so the invariant had teeth
    CHECK(follower.node.counters().fetch_retries > 0); // responses were late, so requests were retried
}

TEST_CASE("converges under loss, duplication and jitter (seed sweep)")
{
    const auto count = seed_count();
    const auto failed = first_failing_seed(count, scenario_convergence);
    CHECK_MESSAGE(!failed.has_value(),
        "failing seed " << (failed ? std::to_string(*failed) : std::string("none")) << " of " << count);
    MESSAGE("convergence sweep: " << count << " seeds ran");
}

TEST_CASE("the seed sweep helper reports the first failing seed")
{
    // The sweep is the evidence for every system-level requirement, so it needs its own test: a
    // scenario that fails on one seed must report that seed, and a clean sweep must report a count.
    const auto failing = first_failing_seed(5, [](std::uint64_t seed) { return seed != 3; });
    REQUIRE(failing.has_value());
    CHECK(*failing == 3);

    const auto clean = first_failing_seed(5, [](std::uint64_t) { return true; });
    CHECK_FALSE(clean.has_value());
    CHECK(seed_count() > 0);
}
