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

    /// `workers` is the set this node ranks against (JOBS-4 would derive it from hello patterns).
    /// Installs the node's record callback, so a node runs either a worker or a requester, not
    /// both.
    JobWorker(Node& node, const Signer& signer, const Hasher& hasher, std::vector<NodeId> workers,
              Channel service, Handler handler);

    /// Run whatever is due. Call from the same loop as `node.tick()`.
    void tick(std::uint64_t now_ms);

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
        std::uint64_t at_ms = 0;     // absolute deadline, fixed on the first tick after it was seen
        bool scheduled = false;
    };

    void on_record(const Record& record);
    bool finished(const std::string& job_id) const {
        return finished_.count(job_id) != 0;
    }

    Node& node_;
    const Signer& signer_;
    const Hasher& hasher_;
    std::vector<NodeId> workers_;
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
    bool submit(const Signer& signer, JobRequest request, std::uint64_t now_ms);

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
