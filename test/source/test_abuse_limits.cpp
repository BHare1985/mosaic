// What an authenticated peer may cost a node: size, pattern count, request rate, queued work
// (spec: security/abuse-limits).
#include <doctest/doctest.h>
#include <mosaic/core/insecure_test_signer.h>
#include <mosaic/jobs/runtime.h>
#include <mosaic/net/sim_network.h>
#include <mosaic/store/memory_log_store.h>

using namespace mosaic;

namespace {
Bytes bytes_of(std::size_t size, char fill = 'x') { return Bytes(size, static_cast<std::uint8_t>(fill)); }

struct Peer {
    SimTransport transport;
    MemoryLogStore store;
    Node node;
    std::vector<std::string> received;
    Peer(SimNetwork& net, const NodeId& id, const Hasher& hasher, const Verifier& verifier, const NodeConfig& config = { })
        : transport(net, id)
        , node(named(config, id), transport, store, hasher, verifier)
    {
        node.on_record([this](const Record& r) { received.emplace_back(r.payload.begin(), r.payload.end()); });
    }
    static NodeConfig named(NodeConfig config, const NodeId& id)
    {
        config.id = id;
        return config;
    }
};

void step(SimNetwork& net, std::uint64_t now, std::initializer_list<Peer*> peers)
{
    net.advance(now);
    for (Peer* peer : peers) {
        peer->node.pump(now);
        peer->node.tick(now);
    }
}
} // namespace

TEST_CASE("an oversized record is refused on ingest and on publish")
{
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(61);

    NodeConfig small;
    small.max_record_bytes = 32;
    small.control_interval_ms = 100;
    Peer writer(net, "writer", hasher, alice, small), follower(net, "follower", hasher, alice, small);
    follower.node.follow("events.>");

    // A local publish over the limit is refused rather than written.
    CHECK_FALSE(writer.node.publish(alice, "events.a", "t", bytes_of(64), 0).has_value());
    CHECK(writer.node.counters().oversized_records == 1);

    // The small one goes through and reaches the follower.
    const auto ok = writer.node.publish(alice, "events.a", "t", bytes_of(8), 0);
    REQUIRE(ok.has_value());
    step(net, 0, { &writer, &follower });
    REQUIRE(follower.received.size() == 1);

    // A validly signed successor that is too large is refused before the store and the callback.
    Record big = *ok;
    big.seq = 1;
    big.prev = record_hash(*ok, hasher);
    big.payload = bytes_of(64);
    big.sig = alice.sign(record_hash(big, hasher));
    writer.transport.send("follower", RecordMsg { big });

    step(net, 50, { &writer, &follower });
    CHECK(follower.received.size() == 1); // unchanged
    CHECK(follower.node.counters().oversized_records == 1);
    REQUIRE(follower.node.head({ "alice", "events.a" }).has_value());
    CHECK(follower.node.head({ "alice", "events.a" })->seq == 0); // the big one is not in the chain
}

TEST_CASE("a head request carrying too many patterns is refused whole")
{
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(63);

    NodeConfig config;
    config.max_head_patterns = 4;
    config.control_interval_ms = 100;
    Peer replica(net, "replica", hasher, alice, config);
    Peer asker(net, "asker", hasher, alice);
    replica.node.follow("events.>");

    GetHeadsMsg hostile;
    for (int i = 0; i < 8; ++i)
        hostile.patterns.push_back("pattern." + std::to_string(i));
    asker.transport.send("replica", hostile);

    step(net, 0, { &replica, &asker });
    CHECK(replica.node.counters().head_requests_refused == 1);
    CHECK(replica.node.counters().heads_answered == 0); // no matcher was built, nothing answered
}

TEST_CASE("per-peer request budgets throttle without disconnecting")
{
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(67);

    NodeConfig config;
    config.max_requests_per_interval = 3;
    config.control_interval_ms = 1000;
    Peer replica(net, "replica", hasher, alice, config);
    Peer asker(net, "asker", hasher, alice);
    replica.node.follow("events.>");

    // Twenty head requests inside one window: only the budget is served.
    for (int i = 0; i < 20; ++i)
        asker.transport.send("replica", GetHeadsMsg { { "events.>" } });

    step(net, 0, { &replica, &asker });
    CHECK(replica.node.counters().requests_throttled > 0);
    CHECK(replica.node.counters().heads_answered <= config.max_requests_per_interval);
    // Throttling is not a partition: the cancelled surplus is counted, the peer is still connected.
    CHECK(replica.node.counters().heads_answered > 0);
}

TEST_CASE("a throttled but honest peer still converges")
{
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(71);

    NodeConfig config;
    config.max_requests_per_interval = 2; // deliberately tight
    config.control_interval_ms = 200;
    config.fetch_batch = 4;
    Peer writer(net, "writer", hasher, alice, config), follower(net, "follower", hasher, alice, config);
    follower.node.follow("events.>");

    for (int i = 0; i < 40; ++i)
        writer.node.publish(alice, "events.a", "t", bytes_of(4, static_cast<char>('a' + (i % 26))), 0);

    std::uint64_t now = 0;
    for (int i = 0; i < 800 && follower.received.size() < 40; ++i) {
        now += 50;
        step(net, now, { &writer, &follower });
    }
    // Throttling slows the repair path down; it must not stop it.
    CHECK(follower.received.size() == 40);
}

TEST_CASE("a worker's pending set is bounded and the overflow is counted")
{
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(73);

    Peer requester(net, "requester", hasher, alice);
    Peer worker(net, "worker", hasher, alice);

    JobWorker jobs(worker.node, alice, hasher, { "worker" }, "resize",
        [](const JobRequest& job) {
            JobResult result;
            result.job_id = job.job_id;
            return result;
        });
    jobs.max_pending = 2; // the bound under test

    for (int i = 0; i < 10; ++i) {
        JobRequest request;
        request.job_id = "job-" + std::to_string(i);
        request.service = "resize";
        request.reply_channel = "res.requester";
        request.timeout_ms = 60000; // long enough that nothing runs during the test
        requester.node.publish(alice, "svc.resize", kJobRequestContentType, encode_job_request(request), 0);
    }

    step(net, 0, { &requester, &worker });
    CHECK(jobs.pending() <= jobs.max_pending);
    CHECK(jobs.pending_overflow > 0);
}
