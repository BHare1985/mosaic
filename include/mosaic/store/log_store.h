#pragma once

#include <mosaic/core/record.h>
#include <mosaic/store/fork_evidence.h>

#include <optional>
#include <vector>

namespace mosaic {

/// What an append promises before it reports success. Named rather than a boolean: "fast" is not a
/// specification, and a deployment has to know what a crash can lose. Every durable backend takes
/// one, so the choice is the backend's contract rather than SQLite's private detail.
enum class Durability {
    PerRecord,  // success means on disk, fsynced before returning (the default)
    Batched,    // durable against process death, not immediately against power loss
};

enum class AppendResult {
    Ok,
    NotNext,  // record.seq is not exactly head+1 (or 0 for an empty log)
};

/// Persistence seam. A LogStore is deliberately dumb: it stores records in order and returns them.
/// All verification (hash chain, signatures, forks) lives in log/ReplicaLog, never here, so every
/// backend (memory, SQLite, segment files) behaves identically. Contract:
/// docs/design/01-data-model.md.
///
/// Implementations are single-threaded unless documented otherwise.
class LogStore {
public:
    virtual ~LogStore() = default;

    /// Append `record` iff record.seq == next sequence of its log. Must be durable (per backend
    /// policy) on Ok.
    virtual AppendResult append(const Record& record) = 0;

    /// Sequence number of the last record, or nullopt for an unknown/empty log.
    virtual std::optional<Seq> head_seq(const LogKey& key) const = 0;

    virtual std::optional<Record> get(const LogKey& key, Seq seq) const = 0;

    /// Up to `count` records starting at `from`, in order. Empty if `from` is past the head.
    virtual std::vector<Record> read(const LogKey& key, Seq from, std::size_t count) const = 0;

    /// Append several records. A backend that can do better than N transactions SHOULD override
    /// this (one commit for a whole repair batch), but the behaviour MUST be exactly that of
    /// appending them one at a time: stop at the first record that is not next, and report it.
    virtual AppendResult append_batch(const std::vector<Record>& records);

    /// Every log this store holds, in key order.
    virtual std::vector<LogKey> keys() const = 0;

    /// Optional: keep this log's checkpoint. The bytes are opaque here - the log layer owns the
    /// format - and a backend that cannot answers false, which is how a caller discovers that
    /// truncation is unavailable rather than believing it happened.
    virtual bool put_checkpoint(const LogKey& /*key*/, const Bytes& /*blob*/) {
        return false;
    }
    /// The checkpoint this store holds for a log, if any. Kept even when the records it covers are
    /// gone: that is the whole point of it.
    virtual std::optional<Bytes> checkpoint(const LogKey& /*key*/) const {
        return std::nullopt;
    }
    /// Optional: drop every record before `first_kept`, whole segments at a time where the backend
    /// has them. Returns how many records went, or nothing when the backend cannot truncate. A
    /// backend that answers MUST NOT leave a readable gap: reads start at `first_kept`.
    virtual std::optional<std::size_t> truncate_before(const LogKey& /*key*/, Seq /*first_kept*/) {
        return std::nullopt;
    }

    /// Optional: a backend that can persist fork evidence returns itself here, so the log layer can
    /// keep evidence across restarts without every implementation having to support it.
    virtual ForkEvidenceStore* evidence() {
        return nullptr;
    }
    virtual const ForkEvidenceStore* evidence() const {
        return nullptr;
    }
};

}  // namespace mosaic
