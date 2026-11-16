#pragma once

// Job envelopes: DESIGN ONLY for now. Implementation tasks are JOBS-1..JOBS-4 in docs/design/TODO.md.
// A job request is an ordinary Record on channel "svc.<service>"; its result is an ordinary Record on
// "res.<requester>". Nothing in log/, store/ or net/ needs to know about jobs.

#include <mosaic/core/types.h>
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

} // namespace mosaic
