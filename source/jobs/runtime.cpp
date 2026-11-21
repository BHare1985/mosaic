#include <mosaic/jobs/runtime.h>

#include <algorithm>

using std::distance;
using std::find;
using std::move;
using std::nullopt;
using std::optional;
using std::size_t;
using std::stable_partition;
using std::string;
using std::uint32_t;
using std::uint64_t;
using std::vector;

namespace mosaic {

JobWorker::JobWorker(Node& node, const Signer& signer, const Hasher& hasher, Channel service,
                     Handler handler)
    : node_(node), signer_(signer), hasher_(hasher), service_(std::move(service)),
      service_channel_("svc." + service_), handler_(std::move(handler)) {
    node_.follow(service_channel_);
    node_.on_record([this](const Record& record) { on_record(record); });
}

JobWorker::JobWorker(Node& node, const Signer& signer, const Hasher& hasher, vector<NodeId> workers,
                     Channel service, Handler handler)
    : node_(node), signer_(signer), hasher_(hasher), workers_(std::move(workers)),
      explicit_workers_(true), service_(std::move(service)), service_channel_("svc." + service_),
      handler_(std::move(handler)) {
    node_.follow(service_channel_);
    node_.on_record([this](const Record& record) { on_record(record); });
}

vector<NodeId> JobWorker::ranking(const string& job_id) const {
    auto ranked = plain_ranking(job_id, node_.now_ms());
    // The view an operator or a test wants: who is asked first, saturated peers moved behind. The
    // *slot* a worker takes comes from the plain order plus a penalty (see tick), because that is
    // the version of the rule two workers with slightly different views cannot disagree about.
    std::ranges::stable_partition(ranked,
                                  [this](const NodeId& id) { return has_spare_capacity(id); });
    return ranked;
}

vector<NodeId> JobWorker::derived_workers() const {
    // The constructor follows the service channel, so this node always belongs in its own ranking;
    // peers never list themselves, so no duplicate arises from that side.
    vector<NodeId> workers = node_.peers_following(service_channel_);
    if (std::ranges::find(workers, node_.id()) == workers.end()) {
        workers.push_back(node_.id());
    }
    return workers;
}

bool JobWorker::has_spare_capacity(const NodeId& peer) const {
    if (peer == node_.id()) {
        return node_.spare_capacity() > 0;
    }
    // peer_capacity counts a stale advertisement as unknown, and unknown is "maybe": a peer that
    // advertises nothing is asked before one that says it is saturated.
    const auto advertised = node_.peer_capacity(peer);
    return !advertised || *advertised > 0;
}

void JobWorker::on_record(const Record& record) {
    if (record.channel == service_channel_) {
        if (record.content_type != kJobRequestContentType) {
            return;
        }
        const auto request = decode_job_request(record.payload);
        if (!request || finished(request->job_id) || pending_.contains(request->job_id)) {
            return;
        }

        // Watch the reply channel so a result published by any worker stops this one from running.
        node_.follow(request->reply_channel);

        // Whether this node is even in the ranking, and where, is decided on the first tick: that
        // is where the clock is, and which peers are banned is a function of time.
        if (pending_.size() >= max_pending) {
            ++pending_overflow;  // bounded queue, counted overflow: never an unbounded map
            return;
        }
        Due due;
        due.request = *request;
        pending_.emplace(request->job_id, std::move(due));
        return;
    }

    if (record.content_type == kJobResultContentType) {
        if (const auto result = decode_job_result(record.payload)) {
            finished_[result->job_id] = true;
        }
    }
}

vector<NodeId> JobWorker::plain_ranking(const string& job_id, uint64_t now_ms) const {
    const vector<NodeId> pool = explicit_workers_ ? workers_ : derived_workers();
    vector<NodeId> candidates;
    candidates.reserve(pool.size());
    for (const auto& worker : pool) {
        if (!node_.peer_banned(worker, now_ms)) {
            candidates.push_back(worker);
        }
    }
    return rendezvous_rank(job_id, candidates, hasher_);
}

void JobWorker::tick(uint64_t now_ms) {
    // What we tell peers about our backlog, recalculated here and advertised on the hello interval.
    // One byte, and coarse on purpose: half the queue is the point where a peer should prefer
    // someone else (D2).
    node_.set_spare_capacity(pending_.size() * 2 >= max_pending ? 0 : 255);

    for (auto it = pending_.begin(); it != pending_.end();) {
        if (finished(it->first)) {
            it = pending_.erase(it);  // somebody else already published a result
            continue;
        }
        if (!it->second.scheduled) {
            const auto ranking = plain_ranking(it->second.request.job_id, now_ms);
            const auto self = std::ranges::find(ranking, node_.id());
            if (self == ranking.end()) {
                it = pending_.erase(it);  // not a worker for this service (any more)
                continue;
            }
            const auto rank = static_cast<uint32_t>(distance(ranking.begin(), self));
            const auto slots = static_cast<uint32_t>(ranking.size());
            const auto slot_ms =
                slots == 0 ? it->second.request.timeout_ms : it->second.request.timeout_ms / slots;
            // The slot comes from the plain rendezvous rank, and a worker that advertises no spare
            // capacity waits one extra cycle - a penalty, not a reshuffle. A penalty is what makes
            // capacity safe: two workers that disagree about someone else's capacity can never
            // disagree about their own rank, so exactly one of them can be due first (JOBS-4, D4).
            auto delay = rank * slot_ms;
            if (!has_spare_capacity(node_.id())) {
                delay += slots * slot_ms;
            }
            it->second.delay_ms = delay;
            it->second.at_ms = now_ms + it->second.delay_ms;
            it->second.scheduled = true;
        }
        if (now_ms < it->second.at_ms) {
            ++it;
            continue;
        }

        const JobRequest request = it->second.request;
        it = pending_.erase(it);
        const JobResult result = handler_(request);
        ++runs_;
        node_.publish(signer_, request.reply_channel, kJobResultContentType,
                      encode_job_result(result), now_ms);
    }
}

JobRequester::JobRequester(Node& node, LogStore& store, Channel reply_channel)
    : node_(node), store_(store), reply_channel_(std::move(reply_channel)) {
    node_.follow(reply_channel_);
    node_.on_record([this](const Record& record) {
        if (record.channel != reply_channel_ || record.content_type != kJobResultContentType) {
            return;
        }
        const auto result = decode_job_result(record.payload);
        if (!result) {
            return;
        }
        if (!pending_.contains(result->job_id)) {
            return;  // a result for a job we are not waiting for
        }
        results_.emplace(result->job_id, *result);  // first result per job id wins
        pending_.erase(result->job_id);
    });
}

size_t JobRequester::resume(const Signer& signer, uint64_t now_ms) {
    // Our own log is the record of what we asked for, so a restart can reconstruct the wait instead
    // of losing the job (task 3.3). The deadline is the request's own timestamp plus its timeout.
    size_t open = 0;
    for (const auto& key : store_.keys()) {
        if (key.writer != signer.id() || !key.channel.starts_with("svc.")) {
            continue;
        }
        const auto head = store_.head_seq(key);
        if (!head) {
            continue;
        }
        for (const auto& record : store_.read(key, 0, *head + 1)) {
            if (record.content_type != kJobRequestContentType) {
                continue;
            }
            const auto request = decode_job_request(record.payload);
            if (!request || results_.contains(request->job_id)) {
                continue;
            }
            const auto deadline = record.timestamp_ms + request->timeout_ms;
            pending_[request->job_id] = Pending{deadline};
            ++open;
        }
    }
    (void)now_ms;  // kept for symmetry with the rest of the runtime; deadlines are absolute
    return open;
}

bool JobRequester::submit(const Signer& signer, const JobRequest& request, uint64_t now_ms) {
    if (request.job_id.empty() || request.timeout_ms == 0) {
        return false;
    }
    const string job_id = request.job_id;
    const auto deadline = now_ms + request.timeout_ms;
    const auto channel = "svc." + request.service;
    node_.publish(signer, channel, kJobRequestContentType, encode_job_request(request), now_ms);
    pending_[job_id] = Pending{deadline};
    return true;
}

optional<JobResult> JobRequester::result_for(const string& job_id) const {
    const auto it = results_.find(job_id);
    if (it == results_.end()) {
        return nullopt;
    }
    return it->second;
}

bool JobRequester::timed_out(const string& job_id, uint64_t now_ms) const {
    const auto it = pending_.find(job_id);
    return it != pending_.end() && now_ms >= it->second.deadline_ms;
}

vector<string> JobRequester::waiting() const {
    vector<string> ids;
    ids.reserve(pending_.size());
    for (const auto& [job_id, pending] : pending_) {
        ids.push_back(job_id);
    }
    return ids;
}

}  // namespace mosaic
