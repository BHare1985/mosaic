#pragma once

#include <mosaic/core/crypto.h>
#include <mosaic/core/types.h>
#include <string>
#include <vector>

namespace mosaic {

/// Rank workers for a job with rendezvous (highest-random-weight) hashing: score(worker) =
/// H(job_id || 0x00 || worker). Highest score first, ties broken by worker id.
///
/// Every node computes the same order from the same inputs, with no coordination, and removing a
/// worker only changes jobs it ranked first. The top-ranked worker runs the job; if no result
/// arrives before the timeout, the next one does. See docs/design/06-jobs.md.
std::vector<NodeId> rendezvous_rank(const std::string& job_id, const std::vector<NodeId>& workers, const Hasher& hasher);

} // namespace mosaic
