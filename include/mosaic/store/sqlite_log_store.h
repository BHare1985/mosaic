#pragma once

// Durable LogStore backend (STORE-1). WAL mode with synchronous=FULL, so "append reported Ok" means
// committed: a caller that saw success can crash immediately and the record is still there.
//
// Records are stored as their wire encoding (`encode_record`: canonical bytes plus signature), which
// keeps one byte format for storage and transmission and means a stored record can be verified by
// anything that can read the log.
//
// A row that does not decode is treated as a damaged tail: the log ends at the last complete record
// instead of failing to open. Contract: openspec/specs/store/log-store.

#include <memory>
#include <mosaic/store/log_store.h>
#include <optional>
#include <string>

namespace mosaic {

class SqliteLogStore final : public LogStore {
public:
    /// Open or create the database at `path` ("":memory:" for a scratch store). Empty on failure.
    static std::optional<std::unique_ptr<SqliteLogStore>> open(const std::string& path);

    ~SqliteLogStore() override;

    SqliteLogStore(const SqliteLogStore&) = delete;
    SqliteLogStore& operator=(const SqliteLogStore&) = delete;

    AppendResult append(const Record& record) override;
    std::optional<Seq> head_seq(const LogKey& key) const override;
    std::optional<Record> get(const LogKey& key, Seq seq) const override;
    std::vector<Record> read(const LogKey& key, Seq from, std::size_t count) const override;
    std::vector<LogKey> keys() const override;

private:
    struct Impl;
    explicit SqliteLogStore(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

} // namespace mosaic
