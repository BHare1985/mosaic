#pragma once

// Durable LogStore backend (STORE-1). WAL mode with synchronous=FULL, so "append reported Ok" means
// committed: a caller that saw success can crash immediately and the record is still there.
//
// Records are stored as their wire encoding (`encode_record`: canonical bytes plus signature),
// which keeps one byte format for storage and transmission and means a stored record can be
// verified by anything that can read the log.
//
// A row that does not decode is treated as a damaged tail: the log ends at the last complete record
// instead of failing to open. Contract: openspec/specs/store/log-store.

#include <mosaic/store/log_store.h>

#include <memory>
#include <optional>
#include <string>

namespace mosaic {

class SqliteLogStore final : public LogStore, public ForkEvidenceStore {
public:
    /// Open or create the database at `path` ("":memory:" for a scratch store). Empty on failure.
    static std::optional<std::unique_ptr<SqliteLogStore>>
    open(const std::string& path, Durability durability = Durability::PerRecord);

    /// The mode this store is running in, for an operator to see.
    Durability durability() const {
        return durability_;
    }

    ~SqliteLogStore() override;

    SqliteLogStore(const SqliteLogStore&) = delete;
    SqliteLogStore& operator=(const SqliteLogStore&) = delete;

    AppendResult append(const Record& record) override;
    /// One transaction for the whole batch: a repair of a large range is one commit, not one per
    /// record.
    AppendResult append_batch(const std::vector<Record>& records) override;
    std::optional<Seq> head_seq(const LogKey& key) const override;
    std::optional<Record> get(const LogKey& key, Seq seq) const override;
    std::vector<Record> read(const LogKey& key, Seq from, std::size_t count) const override;
    std::vector<LogKey> keys() const override;

    /// A checkpoint is one small blob per log, kept in its own table so it survives the rows it
    /// describes being deleted (STORE-3).
    bool put_checkpoint(const LogKey& key, const Bytes& blob) override;
    std::optional<Bytes> checkpoint(const LogKey& key) const override;
    std::optional<std::size_t> truncate_before(const LogKey& key, Seq first_kept) override;

    ForkEvidenceStore* evidence() override {
        return this;
    }
    const ForkEvidenceStore* evidence() const override {
        return this;
    }
    bool save_fork(const ForkEvidence& evidence) override;
    std::vector<ForkEvidence> load_forks(const LogKey& key) const override;

private:
    struct Impl;
    SqliteLogStore(std::unique_ptr<Impl> impl, Durability durability);
    std::unique_ptr<Impl> impl_;
    Durability durability_ = Durability::PerRecord;
};

}  // namespace mosaic
