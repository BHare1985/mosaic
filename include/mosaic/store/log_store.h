#pragma once

#include <mosaic/core/record.h>
#include <optional>
#include <vector>

namespace mosaic {

enum class AppendResult {
    Ok,
    NotNext, // record.seq is not exactly head+1 (or 0 for an empty log)
};

/// Persistence seam. A LogStore is deliberately dumb: it stores records in order and returns them.
/// All verification (hash chain, signatures, forks) lives in log/ReplicaLog, never here, so every
/// backend (memory, SQLite, segment files) behaves identically. Contract: docs/design/01-data-model.md.
///
/// Implementations are single-threaded unless documented otherwise.
class LogStore {
public:
    virtual ~LogStore() = default;

    /// Append `record` iff record.seq == next sequence of its log. Must be durable (per backend policy) on Ok.
    virtual AppendResult append(const Record& record) = 0;

    /// Sequence number of the last record, or nullopt for an unknown/empty log.
    virtual std::optional<Seq> head_seq(const LogKey& key) const = 0;

    virtual std::optional<Record> get(const LogKey& key, Seq seq) const = 0;

    /// Up to `count` records starting at `from`, in order. Empty if `from` is past the head.
    virtual std::vector<Record> read(const LogKey& key, Seq from, std::size_t count) const = 0;

    /// Every log this store holds, in key order.
    virtual std::vector<LogKey> keys() const = 0;
};

} // namespace mosaic
