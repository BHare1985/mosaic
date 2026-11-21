// JOBS: calling a service by name over logs, with no coordinator (spec: jobs/service-calls).
#include <mosaic/core/insecure_test_signer.h>
#include <mosaic/jobs/runtime.h>
#include <mosaic/net/sim_network.h>
#include <mosaic/store/memory_log_store.h>

#include <algorithm>
#include <doctest/doctest.h>

using std::find;
using std::initializer_list;
using std::string;
using std::uint32_t;
using std::uint64_t;
using std::vector;

using namespace mosaic;

namespace {
Bytes bytes(const string& s) {
    return Bytes(s.begin(), s.end());
}

JobRequest request_for(const string& id, const string& requester, uint32_t timeout_ms = 1000) {
    JobRequest request;
    request.job_id = id;
    request.service = "resize";
    request.reply_channel = "res." + requester;
    request.timeout_ms = timeout_ms;
    request.body = bytes("payload-" + id);
    return request;
}

/// A node that can be a worker or a requester; the transports and stores outlive everything else.
struct JobsNode {
    SimTransport transport;
    MemoryLogStore store;
    Node node;
    JobsNode(SimNetwork& net, const NodeId& id, const Hasher& hasher, const Verifier& verifier)
        : transport(net, id), node(config_for(id), transport, store, hasher, verifier) {}
    static NodeConfig config_for(const NodeId& id) {
        NodeConfig config;
        config.id = id;
        return config;
    }
};

void step(SimNetwork& net, uint64_t now_ms, initializer_list<JobsNode*> nodes,
          initializer_list<JobWorker*> workers, initializer_list<JobRequester*> requesters) {
    net.advance(now_ms);
    for (JobsNode* node : nodes) {
        node->node.pump(now_ms);
        node->node.tick(now_ms);
    }
    for (JobWorker* worker : workers)
        worker->tick(now_ms);
    (void)requesters;
}
}  // namespace

TEST_CASE("job envelopes round-trip and reject damage") {
    const auto request = request_for("job-1", "alice");
    const auto decoded = decode_job_request(encode_job_request(request));
    REQUIRE(decoded.has_value());
    CHECK(decoded->job_id == request.job_id);
    CHECK(decoded->service == request.service);
    CHECK(decoded->reply_channel == request.reply_channel);
    CHECK(decoded->timeout_ms == request.timeout_ms);
    CHECK(decoded->body == request.body);

    JobResult result;
    result.job_id = "job-1";
    result.ok = true;
    result.body = bytes("done");
    const auto decoded_result = decode_job_result(encode_job_result(result));
    REQUIRE(decoded_result.has_value());
    CHECK(decoded_result->ok);
    CHECK(decoded_result->body == result.body);

    // A truncated envelope is not a job with defaults; it is nothing.
    auto wire = encode_job_request(request);
    wire.resize(wire.size() - 2);
    CHECK_FALSE(decode_job_request(wire).has_value());
    CHECK_FALSE(decode_job_request(Bytes{}).has_value());
}

TEST_CASE("exactly one of several healthy workers runs a job") {
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(51);

    JobsNode w0(net, "w0", hasher, alice), w1(net, "w1", hasher, alice),
        w2(net, "w2", hasher, alice);
    JobsNode requester(net, "requester", hasher, alice);
    const vector<NodeId> workers{"w0", "w1", "w2"};

    int runs = 0;
    const auto handler = [&runs](const JobRequest& job) {
        ++runs;
        JobResult result;
        result.job_id = job.job_id;
        result.ok = true;
        result.body = Bytes{'o', 'k'};
        return result;
    };

    JobWorker worker0(w0.node, alice, hasher, workers, "resize", handler);
    JobWorker worker1(w1.node, alice, hasher, workers, "resize", handler);
    JobWorker worker2(w2.node, alice, hasher, workers, "resize", handler);
    JobRequester caller(requester.node, requester.store, "res.requester");

    REQUIRE(caller.submit(alice, request_for("job-a", "requester"), 0));

    uint64_t now = 0;
    for (int i = 0; i < 200 && !caller.result_for("job-a"); ++i) {
        now += 25;
        step(net, now, {&w0, &w1, &w2, &requester}, {&worker0, &worker1, &worker2}, {&caller});
    }

    const auto result = caller.result_for("job-a");
    REQUIRE(result.has_value());
    CHECK(result->ok);
    CHECK(runs == 1);  // the whole point: one runner, no coordination
    CHECK(worker0.runs() + worker1.runs() + worker2.runs() == 1);
}

TEST_CASE("a job is not lost when the first-ranked worker never answers") {
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(53);

    JobsNode w0(net, "w0", hasher, alice), w1(net, "w1", hasher, alice);
    JobsNode requester(net, "requester", hasher, alice);

    // Three workers are known, but "ghost" never runs: whichever of these two is not rank 0 must
    // take over after its wait.
    const vector<NodeId> workers{"w0", "w1", "ghost"};
    int runs = 0;
    const auto handler = [&runs](const JobRequest& job) {
        ++runs;
        JobResult result;
        result.job_id = job.job_id;
        result.ok = true;
        return result;
    };

    JobWorker worker0(w0.node, alice, hasher, workers, "resize", handler);
    JobWorker worker1(w1.node, alice, hasher, workers, "resize", handler);
    JobRequester caller(requester.node, requester.store, "res.requester");

    REQUIRE(caller.submit(alice, request_for("job-b", "requester", 600), 0));

    uint64_t now = 0;
    for (int i = 0; i < 400 && !caller.result_for("job-b"); ++i) {
        now += 25;
        step(net, now, {&w0, &w1, &requester}, {&worker0, &worker1}, {&caller});
    }

    REQUIRE(caller.result_for("job-b").has_value());
    CHECK(runs == 1);
}

TEST_CASE("a restarted requester still accepts the result it is waiting for") {
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(59);

    JobsNode requester(net, "requester", hasher, alice), worker(net, "worker", hasher, alice);
    {
        // Ask, then "crash" before the result arrives.
        JobRequester before_restart(requester.node, requester.store, "res.requester");
        REQUIRE(before_restart.submit(alice, request_for("job-r", "requester", 5000), 0));
    }

    // The request is in our own log, so a fresh requester can pick the wait back up.
    JobRequester after_restart(requester.node, requester.store, "res.requester");
    const auto resumed = after_restart.resume(alice, 0);
    CHECK(resumed >= 1);
    CHECK(after_restart.waiting().size() >= 1);
    CHECK_FALSE(after_restart.timed_out("job-r", 0));

    // A result published while we were away is still accept-able.
    JobResult result;
    result.job_id = "job-r";
    result.ok = true;
    result.body = bytes("late");
    worker.node.publish(alice, "res.requester", kJobResultContentType, encode_job_result(result),
                        10);

    uint64_t now = 0;
    for (int i = 0; i < 60 && !after_restart.result_for("job-r"); ++i) {
        now += 25;
        step(net, now, {&requester, &worker}, {}, {&after_restart});
    }
    const auto restored = after_restart.result_for("job-r");
    REQUIRE(restored.has_value());
    CHECK(restored->body == result.body);
}

TEST_CASE("a requester accepts the first result and reports a timeout when none comes") {
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(57);

    JobsNode requester(net, "requester", hasher, alice);
    JobRequester caller(requester.node, requester.store, "res.requester");

    // Nobody is running the service.
    REQUIRE(caller.submit(alice, request_for("job-c", "requester", 200), 0));
    CHECK(caller.waiting().size() == 1);
    CHECK_FALSE(caller.timed_out("job-c", 100));

    uint64_t now = 0;
    for (int i = 0; i < 40; ++i) {
        now += 25;
        step(net, now, {&requester}, {}, {&caller});
    }
    CHECK(caller.timed_out("job-c", now));  // a visible failure, not a silent success
    CHECK_FALSE(caller.result_for("job-c").has_value());

    // Two results for one job id: the first wins and the second is not even the answer we return.
    JobResult first;
    first.job_id = "job-d";
    first.ok = true;
    first.body = bytes("first");
    JobResult second = first;
    second.body = bytes("second");

    // Delivered straight into the requester's log through the network, as another node would.
    JobsNode other(net, "other", hasher, alice);
    JobRequester caller2(requester.node, requester.store,
                         "res.requester");  // re-installs the callback
    REQUIRE(caller2.submit(alice, request_for("job-d", "requester", 1000), now));
    other.node.publish(alice, "res.requester", kJobResultContentType, encode_job_result(first),
                       now);
    other.node.publish(alice, "res.requester", kJobResultContentType, encode_job_result(second),
                       now);

    for (int i = 0; i < 40 && !caller2.result_for("job-d"); ++i) {
        now += 25;
        step(net, now, {&requester, &other}, {}, {&caller2});
    }
    const auto result = caller2.result_for("job-d");
    REQUIRE(result.has_value());
    CHECK(result->body == first.body);
}

TEST_CASE("workers find each other from what they advertise, with no list to keep in step") {
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(61);

    JobsNode w0(net, "w0", hasher, alice), w1(net, "w1", hasher, alice),
        w2(net, "w2", hasher, alice), requester(net, "requester", hasher, alice);

    int runs = 0;
    const auto handler = [&runs](const JobRequest& job) {
        ++runs;
        JobResult result;
        result.job_id = job.job_id;
        result.ok = true;
        return result;
    };
    // No worker list: each one learns the others from their hellos (JOBS-4).
    JobWorker worker0(w0.node, alice, hasher, "resize", handler);
    JobWorker worker1(w1.node, alice, hasher, "resize", handler);
    JobWorker worker2(w2.node, alice, hasher, "resize", handler);
    JobRequester caller(requester.node, requester.store, "res.requester");

    // Let the workers exchange hellos first: what they advertise is what they rank against.
    uint64_t now = 0;
    for (int i = 0; i < 12; ++i) {
        now += 100;
        step(net, now, {&w0, &w1, &w2, &requester}, {&worker0, &worker1, &worker2}, {&caller});
    }
    REQUIRE(worker0.ranking("job-gossip").size() >= 2);  // learned, not supplied

    REQUIRE(caller.submit(alice, request_for("job-gossip", "requester", 600), now));
    for (int i = 0; i < 400 && !caller.result_for("job-gossip"); ++i) {
        now += 25;
        step(net, now, {&w0, &w1, &w2, &requester}, {&worker0, &worker1, &worker2}, {&caller});
    }

    REQUIRE(caller.result_for("job-gossip").has_value());
    CHECK(runs == 1);  // exactly one, and nobody supplied the membership
    // The derived set is what they ranked against, and it grew as hellos arrived.
    CHECK(worker0.ranking("job-gossip").size() >= 2);
}

TEST_CASE("a worker advertising no spare capacity is asked last, not dropped") {
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(67);

    JobsNode me(net, "me", hasher, alice);
    JobsNode saturated(net, "saturated", hasher, alice), idle(net, "idle", hasher, alice);
    int runs = 0;
    JobWorker worker(me.node, alice, hasher, "resize", [&runs](const JobRequest& job) {
        ++runs;
        JobResult result;
        result.job_id = job.job_id;
        result.ok = true;
        return result;
    });

    // Two peers that follow the service; one says it has no room at all.
    saturated.transport.send("me", HelloMsg{kProtocolVersion, {"svc.resize"}, 0});
    idle.transport.send("me", HelloMsg{kProtocolVersion, {"svc.resize"}, 255});
    net.advance(0);
    me.node.pump(0);
    me.node.tick(0);

    const auto ranked = worker.ranking("job-capacity");
    REQUIRE(ranked.size() == 3);          // the two peers and ourselves
    CHECK(ranked.back() == "saturated");  // moved behind, still in the order
    // Saturated does not mean excluded: it is still ranked, so a job still lands somewhere.
    CHECK(find(ranked.begin(), ranked.end(), string("saturated")) != ranked.end());

    // Everyone saturated: the order is still total, so someone is asked first rather than nobody.
    idle.transport.send("me", HelloMsg{kProtocolVersion, {"svc.resize"}, 0});
    me.node.set_spare_capacity(0);
    net.advance(10);
    me.node.pump(10);
    const auto all_saturated = worker.ranking("job-capacity");
    CHECK(all_saturated.size() == 3);  // a total order, not an empty set
    CHECK_FALSE(all_saturated.empty());
    CHECK(find(all_saturated.begin(), all_saturated.end(), string("idle")) != all_saturated.end());
}

TEST_CASE("a worker with a queued job advertises no spare capacity, and drops in the ranking") {
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(71);

    JobsNode busy(net, "busy", hasher, alice), idle(net, "idle", hasher, alice),
        requester(net, "requester", hasher, alice);

    const auto handler = [](const JobRequest& job) {
        JobResult result;
        result.job_id = job.job_id;
        result.ok = true;
        return result;
    };
    // An explicit list, so the ordering under test does not depend on hello timing: "ghost" never
    // runs, which is what holds a job in someone's queue long enough to observe.
    const vector<NodeId> workers{"ghost", "busy", "idle"};
    JobWorker busy_worker(busy.node, alice, hasher, workers, "resize", handler);
    busy_worker.max_pending = 2;  // one queued job is already half full, so it advertises no room
    JobWorker idle_worker(idle.node, alice, hasher, workers, "resize", handler);
    JobRequester caller(requester.node, requester.store, "res.requester");

    uint64_t now = 0;
    for (int i = 0; i < 12; ++i) {  // let capacity advertisements settle
        now += 100;
        step(net, now, {&busy, &idle, &requester}, {&busy_worker, &idle_worker}, {&caller});
    }

    // A job where busy is not rank 0: it waits in the queue instead of running.
    string held;
    for (int i = 0; i < 1000 && held.empty(); ++i) {
        const auto candidate = "job-held-" + std::to_string(i);
        const auto ranking = busy_worker.ranking(candidate);
        if (ranking.size() == 3 && ranking.front() != "busy")
            held = candidate;
    }
    REQUIRE(!held.empty());
    REQUIRE(caller.submit(alice, request_for(held, "requester", 60000), now));
    for (int i = 0; i < 14; ++i) {  // past one hello interval, so the figure reaches the peer
        now += 100;
        step(net, now, {&busy, &idle, &requester}, {&busy_worker, &idle_worker}, {&caller});
    }
    REQUIRE(busy_worker.pending() >= 1);  // waiting for its slot
    CHECK(busy.node.spare_capacity() == 0);

    // The idle worker's own view puts the saturated one behind it, and keeps it in the order rather
    // than dropping it: when everyone is saturated somebody still has to be first.
    const auto ranked = idle_worker.ranking("job-next");
    REQUIRE(ranked.size() == 3);
    CHECK(ranked.back() == "busy");
}
