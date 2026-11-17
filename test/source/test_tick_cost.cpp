// performance/measured-cost: per-tick work must not scale with what is stored.
#include <mosaic/core/insecure_test_signer.h>
#include <mosaic/log/log.h>
#include <mosaic/net/sim_network.h>
#include <mosaic/node/node.h>
#include <mosaic/store/memory_log_store.h>

#include <algorithm>
#include <doctest/doctest.h>
#include <map>

using std::optional;
using std::size_t;
using std::string;
using std::uint64_t;
using std::vector;

using namespace mosaic;

namespace {
Bytes bytes(const string& s) {
    return Bytes(s.begin(), s.end());
}

/// A store that counts the calls a tick makes on it. `keys()` is the interesting one: in the
/// durable backend it is a SELECT DISTINCT over the whole table, so calling it from a loop is a
/// full scan per iteration.
class CountingStore final : public LogStore {
public:
    AppendResult append(const Record& record) override {
        return inner_.append(record);
    }
    optional<Seq> head_seq(const LogKey& key) const override {
        return inner_.head_seq(key);
    }
    optional<Record> get(const LogKey& key, Seq seq) const override {
        return inner_.get(key, seq);
    }
    vector<Record> read(const LogKey& key, Seq from, size_t count) const override {
        return inner_.read(key, from, count);
    }
    vector<LogKey> keys() const override {
        ++keys_calls;
        return inner_.keys();
    }

    MemoryLogStore& inner() {
        return inner_;
    }
    mutable size_t keys_calls = 0;

private:
    MemoryLogStore inner_;
};

struct Peer {
    SimTransport transport;
    CountingStore store;
    Node node;
    vector<string> received;
    Peer(SimNetwork& net, const NodeId& id, const Hasher& hasher, const Verifier& verifier,
         const NodeConfig& config = {})
        : transport(net, id), node(named(config, id), transport, store, hasher, verifier) {
        node.on_record(
            [this](const Record& r) { received.emplace_back(r.payload.begin(), r.payload.end()); });
    }
    static NodeConfig named(NodeConfig config, const NodeId& id) {
        config.id = id;
        return config;
    }
};
}  // namespace

TEST_CASE("a quiet tick does not scan the store") {
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(91);

    NodeConfig config;
    config.control_interval_ms = 50;
    Peer follower(net, "follower", hasher, alice, config);
    follower.node.follow("events.>");

    // Ten ticks with nothing arriving: no work should be done at all, and certainly no store
    // inventory scan.
    uint64_t now = 0;
    const auto before = follower.store.keys_calls;
    for (int i = 0; i < 10; ++i) {
        now += 100;
        net.advance(now);
        follower.node.pump(now);
        follower.node.tick(now);
    }
    CHECK(follower.store.keys_calls - before == 0);
}

TEST_CASE("acknowledgement cost follows ingest, not the number of logs held") {
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(93);

    Peer writer(net, "writer", hasher, alice), follower(net, "follower", hasher, alice);
    follower.node.follow("events.>");

    // Twenty separate logs, all accepted.
    for (int log = 0; log < 20; ++log) {
        for (int i = 0; i < 3; ++i)
            writer.node.publish(alice, "events." + std::to_string(log), "t",
                                bytes(std::to_string(i)), 0);
    }
    uint64_t now = 0;
    for (int i = 0; i < 20; ++i) {
        now += 50;
        net.advance(now);
        writer.node.pump(now);
        writer.node.tick(now);
        follower.node.pump(now);
        follower.node.tick(now);
    }
    REQUIRE(follower.received.size() == 60);

    // From here every tick acknowledges what it holds. The store inventory must never be consulted
    // for it.
    const auto before = follower.store.keys_calls;
    for (int i = 0; i < 20; ++i) {
        now += 100;
        net.advance(now);
        follower.node.pump(now);
        follower.node.tick(now);
    }
    CHECK(follower.store.keys_calls - before == 0);
    CHECK(follower.node.counters().acks_sent >
          0);  // it did acknowledge: the work is real, just bounded
}

TEST_CASE(
    "the retention floor is the min_replicas-th largest acknowledgement, not a sequence scan") {
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(97);

    NodeConfig writer_config;
    writer_config.min_replicas = 2;
    writer_config.control_interval_ms = 50;
    Peer writer(net, "writer", hasher, alice, writer_config);
    Peer r1(net, "r1", hasher, alice, writer_config), r2(net, "r2", hasher, alice, writer_config);
    r1.node.follow("events.>");
    r2.node.follow("events.>");

    for (int i = 0; i < 5; ++i)
        writer.node.publish(alice, "events.a", "t", bytes(std::to_string(i)), 0);

    uint64_t now = 0;
    for (int i = 0; i < 200 && writer.node.retained_records() > 0; ++i) {
        now += 50;
        net.advance(now);
        for (Peer* peer : {&writer, &r1, &r2}) {
            peer->node.pump(now);
            peer->node.tick(now);
        }
    }
    // Two replicas acknowledged everything, so nothing is retained - the same outcome as the
    // sequence-walking version, reached by selecting the second-largest ack instead of counting per
    // sequence.
    CHECK(writer.node.retained_records() == 0);
    CHECK(writer.node.counters().acks_received >= 2);
}
