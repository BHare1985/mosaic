#pragma once

// Fork evidence: when a writer signs two different records for one sequence, a replica keeps both
// so the equivocation can be shown later instead of merely reported (LOG-1). Storage is optional
// and small - a backend that cannot persist it keeps evidence in memory, and the interface below is
// how a backend opts in.
//
// Contract: openspec/specs/log/replicated-log.

#include <mosaic/core/record.h>

#include <cstdint>
#include <vector>

namespace mosaic {

struct ForkEvidence {
    LogKey key;
    Seq seq = 0;
    Record accepted;     // the record we already held and still serve
    Record conflicting;  // the other validly signed record for the same sequence
};

/// A backend that can keep evidence across restarts. Deliberately separate from LogStore: a log
/// store is keyed by (writer, channel, seq) and cannot hold two records at one sequence, which is
/// exactly what evidence is.
class ForkEvidenceStore {
public:
    virtual ~ForkEvidenceStore() = default;

    /// Keep one piece of evidence, keyed by log and sequence (a repeat of the same pair is
    /// idempotent).
    virtual bool save_fork(const ForkEvidence& evidence) = 0;

    /// Everything held for one log, by sequence.
    virtual std::vector<ForkEvidence> load_forks(const LogKey& key) const = 0;
};

}  // namespace mosaic
