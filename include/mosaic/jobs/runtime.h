#pragma once

// Job runtime: a worker that runs a ranked job, and a requester that waits for its result (JOBS-2,
// JOBS-3). Both sit on top of Node's public API, so jobs add no new transport, no new store and no
// coordinator. Semantics: openspec/specs/jobs/service-calls.

#include <mosaic/core/crypto.h>
#include <mosaic/jobs/job.h>
#include <mosaic/jobs/rendezvous.h>
#include <mosaic/node/node.h>

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace mosaic {

/// Runs jobs published on `svc.<service>` when rendezvous ranking says it is this node's turn.
///
/// At-least-once by design: a slow rank 0 and a later rank can both run, so the handler MUST be
/// idempotent by job id. `tick(now_ms)` is what makes the wait elapse - nothing sleeps.
class JobWorker {
public:
    /// Returns the result body and whether it succeeded. The library publishes it; the handler does
    /// not need to know about records, channels or signing.
    using Handler = std::function<JobResult(const JobRequest&)>;

    /// Workers are the peers that advertise following this service's channel, plus this node when
    /// it follows it too - no list to keep in step with reality, and nothing to configure on a node
    /// that joins later (JOBS-4).
    /// Installs the node's record callback, so a node runs either a worker or a requester, not
    /// both.
    JobWorker(Node& node, const Signer& signer, const Hasher& hasher, Channel service,
              Handler handler);

    /// The same, with an explicit worker set that overrides what peers advertise. For a single-node
    /// deployment, or a test, where nobody's hello can say anything.
    JobWorker(Node& node, const Signer& signer, const Hasher& hasher, std::vector<NodeId> workers,
              Channel service, Handler handler);

    /// Run whatever is due. Call from the same loop as `node.tick()`.
    void tick(std::uint64_t now_ms);

    /// The order this node would ask workers in for a job, right now: explicit list or advertised
    /// followers, bans removed, saturated workers moved behind unsaturated ones. Exposed because
    /// "who would we ask, in what order" is the question a test and an operator both have.
    std::vector<NodeId> ranking(const std::string& job_id) const;

    /// Jobs waiting for their turn, and how many this worker has actually run.
    std::size_t pending() const {
        return pending_.size();
    }
    std::uint64_t runs() const {
        return runs_;
    }

    /// Most requests this worker will hold while waiting for its turn. A peer that publishes
    /// requests faster than they retire must not be able to grow this set without bound
    /// (bound-peer-requests).
    std::size_t max_pending = 1024;
    /// Requests dropped because that bound was reached.
    std::uint64_t pending_overflow = 0;

private:
    struct Due {
        JobRequest request;
        std::uint32_t delay_ms = 0;  // rank x slot: how long after seeing it this node may run
        std::uint64_t at_ms = 0;     // absolute deadline, counted from the first tick after arrival
        /// Ranked on the first tick rather than on arrival: ranking needs the clock (to know which
        /// peers are banned) and this class never reads one itself.
        bool scheduled = false;
    };

    void on_record(const Record& record);
    bool finished(const std::string& job_id) const {
        return finished_.count(job_id) != 0;
    }
    /// The ranking this node will act on: the explicit list if one was given, else the peers that
    /// advertise following this service plus ourselves - minus the peers the node currently bans (a
    /// ban it enforces on traffic but not on work would be a ban in name only, SEC-2), with
    /// saturated peers moved behind unsaturated ones.
    std::vector<NodeId> plain_ranking(const std::string& job_id, std::uint64_t now_ms) const;
    /// Peers that advertise following the service channel, plus this node when it follows it.
    std::vector<NodeId> derived_workers() const;
    /// Is this worker worth asking first? Unknown capacity counts as room: a peer that says nothing
    /// must not be punished for it.
    bool has_spare_capacity(const NodeId& peer) const;

    Node& node_;
    const Signer& signer_;
    const Hasher& hasher_;
    std::vector<NodeId> workers_;
    bool explicit_workers_ = false;  // the caller's list wins over what peers advertise
    Channel service_;
    Channel service_channel_;
    Handler handler_;
    std::map<std::string, Due> pending_;
    std::map<std::string, bool> finished_;  // job ids whose result we have seen
    std::uint64_t runs_ = 0;
};

/// Sends a job and waits for the first result for its id.
///
/// Results are records on the requester's own channel, so a restarted requester can still read
/// them: `on_record` collects them and `result_for` hands the first one back.
class JobRequester {
public:
    /// Installs the node's record callback for `reply_channel`. `store` is the same store the node
    /// writes to: it is how a restarted requester finds what it had already asked for.
    JobRequester(Node& node, LogStore& store, Channel reply_channel);

    /// Re-register requests already in our own log, so a restart does not forget what it asked for.
    /// Returns how many were still open.
    std::size_t resume(const Signer& signer, std::uint64_t now_ms);

    /// Publish a request. Remembered so a timeout can be reported and a result accepted later.
    bool submit(const Signer& signer, const JobRequest& request, std::uint64_t now_ms);

    /// The first result for this job id, if it has arrived.
    std::optional<JobResult> result_for(const std::string& job_id) const;

    /// True once `timeout_ms` has passed with no result. Names the job in `JobTimeout` reporting.
    bool timed_out(const std::string& job_id, std::uint64_t now_ms) const;

    /// Job ids this requester is still waiting for.
    std::vector<std::string> waiting() const;

private:
    Node& node_;
    LogStore& store_;
    Channel reply_channel_;
    struct Pending {
        std::uint64_t deadline_ms = 0;
    };
    std::map<std::string, Pending> pending_;
    std::map<std::string, JobResult> results_;  // first result per job id wins
};

}  // namespace mosaic
