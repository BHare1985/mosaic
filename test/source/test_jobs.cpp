// JOBS: calling a service by name over logs, with no coordinator (spec: jobs/service-calls).
#include <mosaic/core/insecure_test_signer.h>
#include <mosaic/jobs/runtime.h>
#include <mosaic/net/sim_network.h>
#include <mosaic/store/memory_log_store.h>

#include <doctest/doctest.h>

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
