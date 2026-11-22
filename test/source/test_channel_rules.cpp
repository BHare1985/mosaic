// security/channel-authorization: who may write a channel, enforced on ingest and on publish.
#include <mosaic/core/insecure_test_signer.h>
#include <mosaic/jobs/runtime.h>
#include <mosaic/net/sim_network.h>
#include <mosaic/store/memory_log_store.h>

#include <doctest/doctest.h>

using std::initializer_list;
using std::string;
using std::uint64_t;
using std::vector;

using namespace mosaic;

namespace {
Bytes bytes(const string& s) {
    return Bytes(s.begin(), s.end());
}

struct Peer {
    SimTransport transport;
    MemoryLogStore store;
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

void step(SimNetwork& net, uint64_t now, initializer_list<Peer*> peers) {
    net.advance(now);
    for (Peer* peer : peers) {
        peer->node.pump(now);
        peer->node.tick(now);
    }
}
}  // namespace

TEST_CASE("channel rules are open by default and match by pattern") {
    ChannelRules rules;
    REQUIRE(rules.allow("audit.>", {"writer-a"}));
    CHECK(rules.permits("audit.eu", "writer-a"));
    CHECK_FALSE(rules.permits("audit.eu", "writer-b"));
    CHECK(rules.permits("events.a", "writer-b"));     // no rule matches: open, as documented
    CHECK_FALSE(rules.allow("bad..pattern", {"x"}));  // invalid patterns are rejected, not stored
}

TEST_CASE("a rule refuses an unlisted writer on ingest and counts it by pattern") {
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice"), mallory("mallory");
    SimNetwork net(81);

    ChannelRules rules;
    REQUIRE(rules.allow("audit.>", {"alice"}));

    // The writer here has no rules of its own: it *can* create anything, which is the situation a
    // follower must defend against.
    Peer writer(net, "writer", hasher, alice), follower(net, "follower", hasher, alice);
    follower.node.set_rules(&rules);
    follower.node.follow(">");

    const auto allowed = writer.node.publish(alice, "audit.eu", "t", bytes("ok"), 0);
    REQUIRE(allowed.has_value());
    step(net, 0, {&writer, &follower});
    REQUIRE(follower.received.size() == 1);

    const auto unlisted = writer.node.publish(mallory, "audit.eu", "t", bytes("nope"), 0);
    REQUIRE(unlisted.has_value());  // the writer can create it; the follower must refuse it

    // Several rounds: the writer keeps announcing its head, and the follower must not fetch a log
    // it will always refuse. One refusal, not one per announcement.
    for (uint64_t now = 50; now <= 500; now += 50)
        step(net, now, {&writer, &follower});

    CHECK(follower.received.size() == 1);
    CHECK_FALSE(follower.node.head({"mallory", "audit.eu"}).has_value());
    CHECK(follower.node.counters().unauthorized_records == 1);
    REQUIRE(follower.node.unauthorized_by_pattern().count("audit.>") == 1);
    CHECK(follower.node.unauthorized_by_pattern().at("audit.>") == 1);
}

TEST_CASE("a writer with rules refuses to publish what its readers would reject") {
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice"), mallory("mallory");
    SimNetwork net(82);

    ChannelRules rules;
    REQUIRE(rules.allow("audit.>", {"alice"}));

    Peer writer(net, "writer", hasher, alice), follower(net, "follower", hasher, alice);
    writer.node.set_rules(&rules);
    follower.node.set_rules(&rules);
    follower.node.follow(">");

    CHECK_FALSE(writer.node.publish(mallory, "audit.eu", "t", bytes("nope"), 0).has_value());
    CHECK(writer.node.head({"mallory", "audit.eu"}).has_value() ==
          false);  // and nothing was appended
    CHECK(writer.node.counters().unauthorized_records == 1);

    // An open channel on the same node is unaffected.
    CHECK(writer.node.publish(mallory, "events.a", "t", bytes("fine"), 0).has_value());
}

TEST_CASE("an unauthorized record is refused before the store and the callback") {
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice"), mallory("mallory");
    SimNetwork net(83);

    ChannelRules rules;
    REQUIRE(rules.allow("audit.>", {"alice"}));

    Peer writer(net, "writer", hasher, alice), follower(net, "follower", hasher, alice);
    follower.node.set_rules(&rules);
    follower.node.follow(">");

    // A record validly signed by mallory. The writer has no rules of its own, so it can create the
    // record - that is exactly the situation the follower must defend against. publish() already
    // delivers it live.
    const auto record =
        writer.node.publish(mallory, "audit.eu", "t", bytes("forged-by-a-stranger"), 0);
    REQUIRE(record.has_value());

    step(net, 0, {&writer, &follower});
    CHECK(follower.received.empty());
    CHECK_FALSE(follower.node.head({"mallory", "audit.eu"}).has_value());
    CHECK(follower.node.counters().unauthorized_records == 1);
}

TEST_CASE("closing svc channels keeps an unlisted peer from making a worker run") {
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice"), mallory("mallory");
    SimNetwork net(87);

    ChannelRules rules;
    REQUIRE(rules.allow("svc.>", {"alice"}));  // only alice may submit jobs

    Peer worker(net, "worker", hasher, alice), stranger(net, "stranger", hasher, alice);
    worker.node.set_rules(&rules);

    int runs = 0;
    JobWorker jobs(worker.node, alice, hasher, {"worker"}, "resize",
                   [&runs](const JobRequest& job) {
                       ++runs;
                       JobResult result;
                       result.job_id = job.job_id;
                       result.ok = true;
                       return result;
                   });

    JobRequest request;
    request.job_id = "job-1";
    request.service = "resize";
    request.reply_channel = "res.requester";
    request.timeout_ms = 1000;
    const auto submitted = stranger.node.publish(mallory, "svc.resize", kJobRequestContentType,
                                                 encode_job_request(request), 0);
    REQUIRE(submitted.has_value());  // the stranger's own node has no rules

    for (uint64_t now = 0; now < 2000; now += 50)
        step(net, now, {&worker, &stranger});

    CHECK(runs == 0);  // refused before the worker ever saw it
    CHECK(worker.node.counters().unauthorized_records >= 1);
}
