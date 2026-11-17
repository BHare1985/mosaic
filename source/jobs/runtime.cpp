#include <mosaic/jobs/runtime.h>

#include <algorithm>

namespace mosaic {

JobWorker::JobWorker(Node& node, const Signer& signer, const Hasher& hasher, std::vector<NodeId> workers, Channel service,
    Handler handler)
    : node_(node)
    , signer_(signer)
    , hasher_(hasher)
    , workers_(std::move(workers))
    , service_(std::move(service))
    , service_channel_("svc." + service_)
    , handler_(std::move(handler))
{
    node_.follow(service_channel_);
    node_.on_record([this](const Record& record) { on_record(record); });
}

void JobWorker::on_record(const Record& record)
{
    if (record.channel == service_channel_) {
        if (record.content_type != kJobRequestContentType)
            return;
        const auto request = decode_job_request(record.payload);
        if (!request || finished(request->job_id) || pending_.count(request->job_id) != 0)
            return;

        // Watch the reply channel so a result published by any worker stops this one from running.
        node_.follow(request->reply_channel);

        const auto ranking = rendezvous_rank(request->job_id, workers_, hasher_);
        const auto self = std::find(ranking.begin(), ranking.end(), node_.id());
        if (self == ranking.end())
            return; // not one of the workers for this service
        const auto rank = static_cast<std::uint32_t>(std::distance(ranking.begin(), self));
        const auto slots = static_cast<std::uint32_t>(ranking.size());
        const auto slot_ms = slots == 0 ? request->timeout_ms : request->timeout_ms / slots;

        if (pending_.size() >= max_pending) {
            ++pending_overflow; // bounded queue, counted overflow: never an unbounded map
            return;
        }
        Due due;
        due.request = *request;
        due.delay_ms = rank * slot_ms; // rank 0 runs immediately, rank 1 after one slot, and so on
        pending_.emplace(request->job_id, std::move(due));
        return;
    }

    if (record.content_type == kJobResultContentType)
        if (const auto result = decode_job_result(record.payload))
            finished_[result->job_id] = true;
}

void JobWorker::tick(std::uint64_t now_ms)
{
    for (auto it = pending_.begin(); it != pending_.end();) {
        if (finished(it->first)) {
            it = pending_.erase(it); // somebody else already published a result
            continue;
        }
        if (!it->second.scheduled) {
            it->second.scheduled = true;
            it->second.at_ms = now_ms + it->second.delay_ms;
        }
        if (now_ms < it->second.at_ms) {
            ++it;
            continue;
        }

        const JobRequest request = it->second.request;
        it = pending_.erase(it);
        const JobResult result = handler_(request);
        ++runs_;
        node_.publish(signer_, request.reply_channel, kJobResultContentType, encode_job_result(result), now_ms);
    }
}

JobRequester::JobRequester(Node& node, LogStore& store, Channel reply_channel)
    : node_(node)
    , store_(store)
    , reply_channel_(std::move(reply_channel))
{
    node_.follow(reply_channel_);
    node_.on_record([this](const Record& record) {
        if (record.channel != reply_channel_ || record.content_type != kJobResultContentType)
            return;
        const auto result = decode_job_result(record.payload);
        if (!result)
            return;
        if (pending_.count(result->job_id) == 0)
            return; // a result for a job we are not waiting for
        results_.emplace(result->job_id, *result); // first result per job id wins
        pending_.erase(result->job_id);
    });
}

std::size_t JobRequester::resume(const Signer& signer, std::uint64_t now_ms)
{
    // Our own log is the record of what we asked for, so a restart can reconstruct the wait instead of
    // losing the job (task 3.3). The deadline is the request's own timestamp plus its timeout.
    std::size_t open = 0;
    for (const auto& key : store_.keys()) {
        if (key.writer != signer.id() || key.channel.rfind("svc.", 0) != 0)
            continue;
        const auto head = store_.head_seq(key);
        if (!head)
            continue;
        for (const auto& record : store_.read(key, 0, *head + 1)) {
            if (record.content_type != kJobRequestContentType)
                continue;
            const auto request = decode_job_request(record.payload);
            if (!request || results_.count(request->job_id) != 0)
                continue;
            const auto deadline = record.timestamp_ms + request->timeout_ms;
            pending_[request->job_id] = Pending { deadline };
            ++open;
        }
    }
    (void)now_ms; // kept for symmetry with the rest of the runtime; deadlines are absolute
    return open;
}

bool JobRequester::submit(const Signer& signer, JobRequest request, std::uint64_t now_ms)
{
    if (request.job_id.empty() || request.timeout_ms == 0)
        return false;
    const std::string job_id = request.job_id;
    const auto deadline = now_ms + request.timeout_ms;
    const auto channel = "svc." + request.service;
    node_.publish(signer, channel, kJobRequestContentType, encode_job_request(request), now_ms);
    pending_[job_id] = Pending { deadline };
    return true;
}

std::optional<JobResult> JobRequester::result_for(const std::string& job_id) const
{
    const auto it = results_.find(job_id);
    if (it == results_.end())
        return std::nullopt;
    return it->second;
}

bool JobRequester::timed_out(const std::string& job_id, std::uint64_t now_ms) const
{
    const auto it = pending_.find(job_id);
    return it != pending_.end() && now_ms >= it->second.deadline_ms;
}

std::vector<std::string> JobRequester::waiting() const
{
    std::vector<std::string> ids;
    for (const auto& [job_id, pending] : pending_)
        ids.push_back(job_id);
    return ids;
}

} // namespace mosaic
