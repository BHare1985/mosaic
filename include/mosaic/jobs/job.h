#pragma once

// Job envelopes (JOBS-1). A job request is an ordinary Record on channel "svc.<service>"; its result is
// an ordinary Record on "res.<requester>". Nothing in log/, store/ or net/ needs to know about jobs,
// which is why a job inherits signing, ordering, durability, replication and replay for free.
//
// Encoding is a small binary format over core/encoding.h rather than a text format: five fields do not
// justify a parser or a dependency. Contract: openspec/specs/jobs/service-calls.

#include <mosaic/core/types.h>
#include <optional>
#include <string>

namespace mosaic {

struct JobRequest {
    std::string job_id; // idempotency key; unique per logical job, chosen by the requester
    std::string service; // channel suffix, e.g. "resize"
    Channel reply_channel; // where the result record should be written
    std::uint32_t timeout_ms = 5000;
    Bytes body;
};

struct JobResult {
    std::string job_id;
    bool ok = false;
    Bytes body; // output on success, error text on failure
};

/// Content types a job record carries, so a reader can tell a job envelope from application data.
inline constexpr const char* kJobRequestContentType = "application/mosaic-job-request";
inline constexpr const char* kJobResultContentType = "application/mosaic-job-result";

Bytes encode_job_request(const JobRequest& request);
std::optional<JobRequest> decode_job_request(const Bytes& bytes);

Bytes encode_job_result(const JobResult& result);
std::optional<JobResult> decode_job_result(const Bytes& bytes);

} // namespace mosaic
