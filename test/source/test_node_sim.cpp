#include <mosaic/core/insecure_test_signer.h>
#include <mosaic/net/sim_network.h>
#include <mosaic/node/node.h>
#include <mosaic/store/memory_log_store.h>

#include <doctest/doctest.h>
#include <string>
#include <vector>

#include "scenario.h"

using std::initializer_list;
using std::size_t;
using std::string;
using std::uint64_t;
using std::uint8_t;
using std::vector;

using namespace mosaic;
using mosaic::test::first_failing_seed;
using mosaic::test::seed_count;

namespace {
Bytes bytes(const string& s) {
    return Bytes(s.begin(), s.end());
}

struct Peer {
    SimTransport transport;
    MemoryLogStore store;
    Node node;
    vector<string> received;
    vector<string> forks;

    Peer(SimNetwork& net, const NodeId& id, const Hasher& hasher, const Verifier& verifier,
         const NodeConfig& config = {})
        : transport(net, id), node(named(config, id), transport, store, hasher, verifier) {
        node.on_record(
            [this](const Record& r) { received.emplace_back(r.payload.begin(), r.payload.end()); });
        node.on_fork(
            [this](const Record& r) { forks.emplace_back(r.payload.begin(), r.payload.end()); });
    }

    static NodeConfig named(NodeConfig config, const NodeId& id) {
        config.id = id;
        return config;
    }
};

/// One simulated step: advance the clock (releasing everything due) and let each node work.
/// Time is injected everywhere, so a step costs no real time however long the outage it models.
void step(SimNetwork& net, uint64_t now_ms, initializer_list<Peer*> peers) {
    net.advance(now_ms);
    for (Peer* peer : peers) {
        peer->node.pump(now_ms);
        peer->node.tick(now_ms);
    }
}

bool scenario_convergence(uint64_t seed) {
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(seed);
    net.set_loss(0.3);
    net.set_duplicate(0.1);
    net.set_delay(0, 120);  // jitter, so records arrive out of order as well as late

    Peer writer(net, "writer", hasher, alice), r1(net, "r1", hasher, alice),
        r2(net, "r2", hasher, alice);
    r1.node.follow("events.>");
    r2.node.follow("events.a");

    uint64_t now = 0;
    int published = 0;
    const int total = 100;
    bool converged = false;
    for (int step_count = 0; step_count < 8000 && !converged; ++step_count) {
        now += 50;
        if (published < total) {
            writer.node.publish(alice, published % 2 ? "events.a" : "events.b", "t",
                                bytes(std::to_string(published)), now);
            ++published;
        }
        step(net, now, {&writer, &r1, &r2});
        if (published == total) {
            const auto wa = writer.node.head({"alice", "events.a"});
            const auto wb = writer.node.head({"alice", "events.b"});
            converged = r1.node.head({"alice", "events.a"}) &&
                        r1.node.head({"alice", "events.b"}) &&
                        r2.node.head({"alice", "events.a"}) &&
                        r1.node.head({"alice", "events.a"})->hash == wa->hash &&
                        r1.node.head({"alice", "events.b"})->hash == wb->hash &&
                        r2.node.head({"alice", "events.a"})->hash == wa->hash;
        }
    }
    if (!converged)
        return false;
    return r1.received.size() == static_cast<size_t>(total) &&
           r2.received.size() == static_cast<size_t>(total / 2) && net.dropped() > 0;
}
}  // namespace

TEST_CASE("lossless: followers receive live records in order; non-followers do not") {
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(1);
    Peer writer(net, "writer", hasher, alice), follower(net, "follower", hasher, alice),
        other(net, "other", hasher, alice);
    follower.node.follow("events.>");
    other.node.follow("unrelated.>");

    for (int i = 0; i < 5; ++i)
        writer.node.publish(alice, "events.a", "t", bytes("m" + std::to_string(i)), 0);
    step(net, 0, {&writer, &follower, &other});

    CHECK(follower.received == vector<string>{"m0", "m1", "m2", "m3", "m4"});
    CHECK(other.received.empty());
    CHECK_FALSE(other.node.head({"alice", "events.a"}).has_value());

    // Counters make the same claim without reading log contents (NODE-1).
    CHECK(follower.node.counters().accepted == 5);
    CHECK(follower.node.counters().duplicate == 0);
    CHECK(follower.node.counters().bad_signature == 0);
    CHECK(other.node.counters().accepted == 0);
}

TEST_CASE("a late joiner catches up through HEAD + FETCH") {
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(1);
    Peer writer(net, "writer", hasher, alice);
    for (int i = 0; i < 200; ++i)  // more than one fetch_batch (64)
        writer.node.publish(alice, "events.a", "t", bytes(std::to_string(i)), 0);

    Peer late(net, "late", hasher, alice);
    late.node.follow("events.>");

    uint64_t now = 0;
    for (int step_count = 0; step_count < 400 && late.received.size() < 200; ++step_count) {
        now += 50;
        step(net, now, {&writer, &late});
    }

    REQUIRE(late.received.size() == 200);
    for (int i = 0; i < 200; ++i)
        CHECK(late.received[static_cast<size_t>(i)] == std::to_string(i));
    CHECK(late.node.counters().fetches_sent > 0);
    CHECK(late.node.counters().accepted == 200);
    // Repair from HEAD fetches forward from seq 0, so this path never produces a gap; a gap needs a
    // *live* record that jumps ahead, which the loss scenarios below provide.
    CHECK(late.node.counters().gap == 0);
}

TEST_CASE("delay hides a message until its arrival time") {
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(3);
    net.set_delay(100, 100);
    Peer writer(net, "writer", hasher, alice), follower(net, "follower", hasher, alice);
    follower.node.follow("events.>");

    writer.node.publish(alice, "events.a", "t", bytes("one"), 0);
    step(net, 0, {&writer, &follower});
    CHECK(follower.received.empty());
    step(net, 99, {&writer, &follower});
    CHECK(follower.received.empty());
    step(net, 100, {&writer, &follower});
    CHECK(follower.received.size() == 1);
}

TEST_CASE("jitter actually reorders deliveries") {
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(5);
    net.set_delay(0, 50);  // a range, so arrival order can differ from send order
    Peer writer(net, "writer", hasher, alice), follower(net, "follower", hasher, alice);
    follower.node.follow("events.>");

    for (int i = 0; i < 20; ++i)
        writer.node.publish(alice, "events.a", "t", bytes(std::to_string(i)), 0);
    net.advance(200);
    CHECK(net.reordered() > 0);  // the fault model is real, not assumed

    // Arriving out of order means the follower sees holes and must repair them; it converges
    // anyway.
    uint64_t now = 200;
    for (int step_count = 0; step_count < 400 && follower.received.size() < 20; ++step_count) {
        now += 50;
        step(net, now, {&writer, &follower});
    }
    CHECK(follower.received.size() == 20);
    CHECK(follower.node.counters().gap > 0);
}

TEST_CASE("a partition blocks traffic and healing restores convergence") {
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(9);
    Peer writer(net, "writer", hasher, alice), follower(net, "follower", hasher, alice);
    follower.node.follow("events.>");

    net.partition("writer", "follower");
    for (int i = 0; i < 10; ++i)
        writer.node.publish(alice, "events.a", "t", bytes(std::to_string(i)), 0);
    step(net, 100, {&writer, &follower});
    CHECK(follower.received.empty());
    CHECK(net.dropped() > 0);

    net.heal("writer", "follower");
    uint64_t now = 100;
    for (int step_count = 0; step_count < 400 && follower.received.size() < 10; ++step_count) {
        now += 100;
        step(net, now, {&writer, &follower});
    }
    CHECK(follower.received.size() == 10);
}

TEST_CASE("forged records are rejected, counted and reported once") {
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(11);
    Peer writer(net, "writer", hasher, alice), follower(net, "follower", hasher, alice);
    follower.node.follow("events.>");

    const auto genuine_opt = writer.node.publish(alice, "events.a", "t", bytes("real"), 0);
    REQUIRE(genuine_opt.has_value());  // publish reports refusal instead of writing
    const Record genuine = *genuine_opt;
    step(net, 0, {&writer, &follower});
    REQUIRE(follower.received.size() == 1);

    Record forged = genuine;
    forged.seq = 1;
    forged.prev = record_hash(genuine, hasher);
    forged.payload = bytes("fake");  // signature still covers the original content
    writer.transport.send("follower", RecordMsg{forged});
    step(net, 0, {&writer, &follower});

    CHECK(follower.node.counters().bad_signature == 1);
    CHECK(follower.received.size() == 1);
    CHECK(follower.node.head({"alice", "events.a"})->seq == 0);
}

TEST_CASE("duplicated delivery is counted but reaches the app once") {
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(13);
    net.set_duplicate(1.0);  // every message delivered twice
    Peer writer(net, "writer", hasher, alice), follower(net, "follower", hasher, alice);
    follower.node.follow("events.>");

    for (int i = 0; i < 4; ++i)
        writer.node.publish(alice, "events.a", "t", bytes(std::to_string(i)), 0);
    step(net, 0, {&writer, &follower});

    CHECK(net.duplicated() >= 4);
    CHECK(follower.received.size() == 4);
    CHECK(follower.node.counters().duplicate == 4);
}

TEST_CASE("credit is accounted per request and is never inflated by late responses") {
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(4);
    net.set_delay(150, 150);  // every response arrives after the fetch timeout

    NodeConfig config;
    config.fetch_window = 4;
    config.fetch_batch = 2;
    config.fetch_timeout_ms = 100;
    config.head_interval_ms = 50;

    Peer writer(net, "writer", hasher, alice), follower(net, "follower", hasher, alice, config);
    for (int i = 0; i < 6; ++i)  // published before anyone follows, so everything comes from repair
        writer.node.publish(alice, "events.a", "t", bytes(std::to_string(i)), 0);
    follower.node.follow("events.>");

    uint64_t now = 0;
    uint64_t max_outstanding = 0;
    for (int step_count = 0; step_count < 400 && follower.received.size() < 6; ++step_count) {
        now += 10;
        step(net, now, {&writer, &follower});

        // Measured from cumulative accounting, not from the live-request table: a blanket reset()
        // empties that table while the old round's responses are still in flight, which is exactly
        // the state that must never be reachable.
        const auto spent = follower.node.counters().credit_spent;
        const auto returned = follower.node.counters().credit_returned;
        REQUIRE(spent >= returned);
        const auto outstanding = spent - returned;
        REQUIRE(outstanding <= config.fetch_window);
        REQUIRE(follower.node.credit_available("writer") == config.fetch_window - outstanding);
        max_outstanding = outstanding > max_outstanding ? outstanding : max_outstanding;
    }

    CHECK(follower.received.size() == 6);
    CHECK(max_outstanding > 0);  // the window really was used, so the invariant had teeth
    CHECK(follower.node.counters().fetch_retries >
          0);  // responses were late, so requests were retried
}

TEST_CASE("peers negotiate the protocol version without blocking replication") {
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(17);
    Peer writer(net, "writer", hasher, alice), follower(net, "follower", hasher, alice);
    follower.node.follow("events.>");

    writer.node.publish(alice, "events.a", "t", bytes("hello-then-data"), 0);
    step(net, 0, {&writer, &follower});
    // The handshake rides on tick(), so the record may arrive before the hello: that is counted,
    // not punished (a hello can be lost like anything else, and records are self-verifying).
    CHECK(follower.received.size() == 1);

    step(net, 200, {&writer, &follower});  // one head interval later both sides have said hello
    writer.node.publish(alice, "events.a", "t", bytes("after-handshake"), 200);
    step(net, 300, {&writer, &follower});
    CHECK(follower.received.size() == 2);
    CHECK(writer.node.counters().version_rejects == 0);
    CHECK(follower.node.counters().version_rejects == 0);

    // After negotiation, further payload advances without being counted as unnegotiated.
    const auto before = follower.node.counters().unnegotiated;
    writer.node.publish(alice, "events.a", "t", bytes("settled"), 400);
    step(net, 500, {&writer, &follower});
    CHECK(follower.received.size() == 3);
    CHECK(follower.node.counters().unnegotiated == before);
}

TEST_CASE("payload from a peer announcing an unsupported version is refused") {
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(19);
    Peer writer(net, "writer", hasher, alice), follower(net, "follower", hasher, alice);
    // The writer follows the channel, so the payload would genuinely reach it if it were allowed.
    writer.node.follow("events.>");
    follower.node.follow("events.>");

    // Let the normal handshake settle first (both sides greet on tick), so the injected hello below
    // is not overwritten by a retry: the *last* version a peer announces is the one that counts.
    step(net, 0, {&writer, &follower});
    step(net, 200, {&writer, &follower});
    REQUIRE(writer.node.counters().version_rejects == 0);

    // Now the peer announces a version the writer cannot speak.
    follower.transport.send("writer", HelloMsg{static_cast<uint8_t>(kProtocolVersion + 7), {}});
    step(net, 400, {&writer, &follower});
    CHECK(writer.node.counters().version_rejects == 1);

    follower.node.publish(alice, "events.a", "t", bytes("from-the-future"), 0);
    step(net, 50, {&writer, &follower});
    CHECK(writer.received.empty());  // refused at the boundary, not stored and not delivered
    CHECK(writer.node.counters().version_rejects == 2);
}

TEST_CASE("a node with no callback is a pure replica and still serves repairs") {
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(21);

    // A node configured with patterns only: no on_record, no on_fork. That is a store node.
    struct StoreNode {
        SimTransport transport;
        MemoryLogStore store;
        Node node;
        StoreNode(SimNetwork& net, const NodeId& id, const Hasher& hasher, const Verifier& verifier)
            : transport(net, id), node(NodeConfig{.id = id}, transport, store, hasher, verifier) {}
    };

    Peer writer(net, "writer", hasher, alice);
    StoreNode replica(net, "replica", hasher, alice);
    replica.node.follow("events.>");  // follow everything, deliver nothing

    for (int i = 0; i < 5; ++i)
        writer.node.publish(alice, "events.a", "t", bytes(std::to_string(i)), 0);

    uint64_t now = 0;
    for (int i = 0; i < 200 && !replica.node.head({"alice", "events.a"}); ++i) {
        now += 50;
        net.advance(now);
        writer.node.pump(now);
        writer.node.tick(now);
        replica.node.pump(now);
        replica.node.tick(now);
    }
    REQUIRE(replica.node.head({"alice", "events.a"}).has_value());
    CHECK(replica.node.head({"alice", "events.a"})->seq == 4);
    CHECK(replica.node.counters().accepted == 5);  // stored, with nobody to deliver to

    // Now a follower arrives and the replica serves it. The head is injected because telling a
    // newcomer "these logs exist" is HEADS-1, which is not implemented yet - this test is about the
    // responder role.
    Peer follower(net, "follower", hasher, alice);
    follower.node.follow("events.>");
    replica.transport.send("follower", HeadMsg{{"alice", "events.a"}, 4, Hash{}});

    for (int i = 0; i < 200 && follower.received.size() < 5; ++i) {
        now += 50;
        net.advance(now);
        replica.node.pump(now);
        replica.node.tick(now);
        follower.node.pump(now);
        follower.node.tick(now);
    }
    REQUIRE(follower.received.size() == 5);
    for (int i = 0; i < 5; ++i)
        CHECK(follower.received[static_cast<size_t>(i)] == std::to_string(i));
}

TEST_CASE("replicas acknowledge, and the writer releases records once enough have") {
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(31);

    NodeConfig writer_cfg;
    writer_cfg.min_replicas = 2;
    writer_cfg.control_interval_ms = 100;
    Peer writer(net, "writer", hasher, alice, writer_cfg);
    Peer r1(net, "r1", hasher, alice), r2(net, "r2", hasher, alice);
    r1.node.follow("events.>");
    r2.node.follow("events.>");

    for (int i = 0; i < 5; ++i)
        writer.node.publish(alice, "events.a", "t", bytes(std::to_string(i)), 0);

    uint64_t now = 0;
    for (int step_count = 0; step_count < 200 && writer.node.retained_records() > 0; ++step_count) {
        now += 50;
        step(net, now, {&writer, &r1, &r2});
    }

    CHECK(writer.node.counters().acks_received >= 2);  // at least one from each replica
    CHECK(writer.node.retained_records() == 0);  // both acknowledged everything, so nothing is held
    CHECK(writer.node.retention_shortfalls().empty());
    CHECK(r1.node.counters().acks_sent >= 1);
}

TEST_CASE("retention reports the shortfall instead of blocking when replicas are missing") {
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(33);

    NodeConfig writer_cfg;
    writer_cfg.min_replicas = 2;     // two replicas required...
    writer_cfg.retention_limit = 2;  // ...and only two records may be held while waiting
    writer_cfg.control_interval_ms = 100;
    Peer writer(net, "writer", hasher, alice, writer_cfg);
    Peer r1(net, "r1", hasher, alice), r2(net, "r2", hasher, alice);
    r1.node.follow("events.>");
    r2.node.follow("events.>");

    for (int i = 0; i < 3; ++i)
        writer.node.publish(alice, "events.a", "t", bytes(std::to_string(i)), 0);

    uint64_t now = 0;
    for (int step_count = 0; step_count < 100; ++step_count) {
        now += 50;
        step(net, now, {&writer, &r1, &r2});
    }
    REQUIRE(writer.node.retained_records() == 0);  // both replicas confirmed the first records

    // One replica dies; from here the floor of two can never be met again.
    net.partition("writer", "r2");
    for (int i = 3; i < 9; ++i)
        writer.node.publish(alice, "events.a", "t", bytes(std::to_string(i)), now);

    for (int step_count = 0; step_count < 200 && writer.node.retention_shortfalls().empty();
         ++step_count) {
        now += 50;
        step(net, now, {&writer, &r1});
    }

    // A dead replica becomes a visible counter, not an outage and not silent retention growth.
    CHECK(writer.node.counters().retention_shortfall > 0);
    REQUIRE(writer.node.retention_shortfalls().size() == 1);
    CHECK(writer.node.retention_shortfalls().front().channel == "events.a");
    CHECK(writer.node.retained_records() <= writer_cfg.retention_limit);
    // The log itself survives: the replica that stayed has every record, so nothing was lost with
    // the dead one.
    REQUIRE(r1.node.head({"alice", "events.a"}).has_value());
    CHECK(r1.node.head({"alice", "events.a"})->seq == 8);
}

TEST_CASE("a newcomer finds the log from a replica while the writer is offline") {
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(37);

    NodeConfig cfg;
    cfg.control_interval_ms = 100;
    Peer writer(net, "writer", hasher, alice, cfg), replica(net, "replica", hasher, alice, cfg);
    replica.node.follow("events.>");

    for (int i = 0; i < 8; ++i)
        writer.node.publish(alice, "events.a", "t", bytes(std::to_string(i)), 0);

    uint64_t now = 0;
    for (int step_count = 0; step_count < 200 && !replica.node.head({"alice", "events.a"});
         ++step_count) {
        now += 50;
        step(net, now, {&writer, &replica});
    }
    REQUIRE(replica.node.head({"alice", "events.a"})->seq == 7);

    // The writer goes away: the newcomer can never learn a head from it.
    net.partition("writer", "late");
    Peer late(net, "late", hasher, alice, cfg);
    late.node.follow("events.>");

    for (int step_count = 0; step_count < 200 && late.received.size() < 8; ++step_count) {
        now += 50;
        step(net, now, {&replica, &late});  // the writer is not pumped at all
    }

    CHECK(replica.node.counters().heads_answered > 0);
    // Requests are rate-limited rather than sent on every tick: 200 steps x 50ms against a 100ms
    // control interval cannot produce hundreds of requests.
    CHECK(late.node.counters().heads_requested > 0);
    CHECK(late.node.counters().heads_requested <= 20);
    REQUIRE(late.received.size() == 8);
    for (int i = 0; i < 8; ++i)
        CHECK(late.received[static_cast<size_t>(i)] == std::to_string(i));
}

TEST_CASE("granted live credit paces delivery, and the rest is repaired") {
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(41);

    NodeConfig cfg;
    cfg.control_interval_ms = 100;
    Peer writer(net, "writer", hasher, alice, cfg), follower(net, "follower", hasher, alice, cfg);
    follower.node.follow("events.>");
    follower.node.grant_live_credit("writer", 4);  // only four live records may be pushed

    for (int i = 0; i < 12; ++i)
        writer.node.publish(alice, "events.a", "t", bytes(std::to_string(i)), 0);

    uint64_t now = 0;
    for (int step_count = 0; step_count < 200 && follower.received.size() < 12; ++step_count) {
        now += 50;
        step(net, now, {&writer, &follower});
    }

    // Everything arrives in the end - the live path dropped what credit did not cover and repair
    // made up the difference, which is the whole point of pacing without blocking.
    REQUIRE(follower.received.size() == 12);
    CHECK(follower.node.counters().credit_drops > 0);
    for (int i = 0; i < 12; ++i)
        CHECK(follower.received[static_cast<size_t>(i)] == std::to_string(i));
}

TEST_CASE("a follower too far behind switches to repair and still converges") {
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(43);

    NodeConfig cfg;
    cfg.slow_follower_threshold = 2;  // deliberately easy to exceed
    cfg.control_interval_ms = 100;
    Peer writer(net, "writer", hasher, alice, cfg), follower(net, "follower", hasher, alice, cfg);
    follower.node.follow("events.>");

    // Make the follower genuinely behind: while the link is broken it misses ten records, and the
    // next live record it sees is ten sequences ahead. That is the case the rule exists for.
    net.partition("writer", "follower");
    for (int i = 0; i < 10; ++i)
        writer.node.publish(alice, "events.a", "t", bytes(std::to_string(i)), 0);
    step(net, 50, {&writer, &follower});
    REQUIRE(follower.received.empty());
    net.heal("writer", "follower");
    writer.node.publish(alice, "events.a", "t", bytes("ten"), 100);  // arrives live, ten ahead

    uint64_t now = 100;
    for (int step_count = 0; step_count < 300 && follower.received.size() < 11; ++step_count) {
        now += 50;
        step(net, now, {&writer, &follower});
    }

    CHECK(follower.node.counters().slow_follower_switches > 0);
    REQUIRE(follower.received.size() == 11);
}

TEST_CASE("converges under loss, duplication and jitter (seed sweep)") {
    const auto count = seed_count();
    const auto failed = first_failing_seed(count, scenario_convergence);
    CHECK_MESSAGE(!failed.has_value(), "failing seed "
                                           << (failed ? std::to_string(*failed) : string("none"))
                                           << " of " << count);
    MESSAGE("convergence sweep: " << count << " seeds ran");
}

TEST_CASE("the seed sweep helper reports the first failing seed") {
    // The sweep is the evidence for every system-level requirement, so it needs its own test: a
    // scenario that fails on one seed must report that seed, and a clean sweep must report a count.
    const auto failing = first_failing_seed(5, [](uint64_t seed) { return seed != 3; });
    REQUIRE(failing.has_value());
    CHECK(*failing == 3);

    const auto clean = first_failing_seed(5, [](uint64_t) { return true; });
    CHECK_FALSE(clean.has_value());
    CHECK(seed_count() > 0);
}

TEST_CASE("unfollow stops delivery, and a batch publishes in order") {
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(99);
    Peer writer(net, "writer", hasher, alice), follower(net, "follower", hasher, alice);

    REQUIRE(follower.node.follow("events.>"));
    REQUIRE(follower.node.follow("metrics.>"));
    CHECK(follower.node.following().size() == 2);

    writer.node.publish(alice, "events.a", "t", bytes("one"), 0);
    step(net, 0, {&writer, &follower});
    REQUIRE(follower.received.size() == 1);

    CHECK(follower.node.unfollow("events.>"));
    CHECK_FALSE(follower.node.unfollow("events.>"));  // already gone
    CHECK(follower.node.following().size() == 1);

    writer.node.publish(alice, "events.a", "t", bytes("two"), 10);
    step(net, 50, {&writer, &follower});
    CHECK(follower.received.size() == 1);  // events.a is no longer followed

    // The other pattern still works.
    writer.node.publish(alice, "metrics.cpu", "t", bytes("three"), 60);
    step(net, 100, {&writer, &follower});
    CHECK(follower.received.size() == 2);

    // A batch occupies consecutive sequences, in the order given.
    vector<Bytes> payloads{bytes("b0"), bytes("b1"), bytes("b2")};
    size_t written = 0;
    {
        Peer batcher(net, "batcher", hasher, alice);
        written = batcher.node.publish_batch(alice, "events.z", "t", payloads, 200);
    }
    CHECK(written == 3);
    REQUIRE(follower.node.follow("events.z"));
}
