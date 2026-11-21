#pragma once

#include <mosaic/core/crypto.h>
#include <mosaic/core/record.h>
#include <mosaic/store/fork_evidence.h>
#include <mosaic/store/log_store.h>

#include <functional>
#include <map>
#include <optional>
#include <vector>

namespace mosaic {

/// Outcome of offering a record to a replica. This enum is the vocabulary the rest of the system
/// (Node, tests, metrics) uses to talk about log health; see docs/design/01-data-model.md.
enum class IngestResult {
    Accepted,   // next record in the chain, verified and stored
    Duplicate,  // already have exactly this record
    Gap,        // seq is beyond head+1: caller should FETCH the missing range
    Fork,       // same writer+channel+seq (or prev) but different content: equivocation evidence
    BadChain,   // structurally invalid chain (e.g. seq 0 with non-zero prev)
    BadSignature,
    NotAttempted,  // only in a batch: the run stopped before this record (a gap or a refusal ahead)
};

const char* to_string(IngestResult result);

/// Writer side of a log: assigns seq, links prev, signs, stores. One LogWriter per (signer,
/// channel). Resumes from the store if the log already has records. The Signer must outlive the
/// LogWriter.
class LogWriter {
public:
    LogWriter(LogStore& store, const Hasher& hasher, const Signer& signer, Channel channel);

    Record append(std::string content_type, Bytes payload, std::uint64_t timestamp_ms);

    std::optional<Head> head() const;
    const LogKey& key() const {
        return key_;
    }

private:
    LogStore& store_;
    const Hasher& hasher_;
    const Signer& signer_;
    LogKey key_;
};

/// Replica side of a log: verifies and stores records written by other writers.
/// Never trusts the sender: signature and chain are checked on every record.
class ReplicaLog {
public:
    ReplicaLog(LogStore& store, const Hasher& hasher, const Verifier& verifier);

    IngestResult ingest(const Record& record);

    /// Offer a run of records, verifying each one and appending contiguous stretches with one
    /// `append_batch` per log. The outcomes are exactly what `ingest` would have reported one at a
    /// time; records after the first refusal in a log are `NotAttempted`, because a caller
    /// repairing a gap needs to know what it did not get rather than guess (LOG-2).
    std::vector<IngestResult> ingest_batch(const std::vector<Record>& records);

    /// Where the chain starts when the predecessor is no longer stored (a truncated log). Asked
    /// with the sequence whose hash is needed; returning nothing means "unknown", and the record is
    /// refused as before. A checkpoint is the only honest answer, which is why this is a hook and
    /// not a store query (STORE-3).
    void set_origin(std::function<std::optional<Hash>(const LogKey&, Seq)> origin) {
        origin_ = std::move(origin);
    }

    /// Evidence of writer equivocation: both records for a sequence, bounded by `max_evidence`.
    /// Keeping it is what turns "a fork was detected" into something that can be shown later
    /// (LOG-1).
    std::vector<ForkEvidence> forks() const;
    std::size_t max_evidence =
        1024;  // configuration, not policy: bound the memory a fork storm can cost
    std::uint64_t evidence_overflow = 0;  // evidence dropped because the bound was reached

    std::optional<Head> head(const LogKey& key) const;

private:
    /// Record both sides of a fork. Returns the evidence so the caller can hand it to the
    /// application.
    void keep_evidence(const Record& stored, const Record& conflicting);

    LogStore& store_;
    const Hasher& hasher_;
    const Verifier& verifier_;
    std::map<LogKey, std::map<Seq, ForkEvidence>> evidence_;
    std::function<std::optional<Hash>(const LogKey&, Seq)> origin_;
};

}  // namespace mosaic
