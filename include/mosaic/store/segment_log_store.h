#pragma once

// Durable LogStore backend whose layout is the log's own shape (STORE-2): one directory per log, a
// chain of append-only segment files, and an index that is derived state the segment can always
// rebuild. The format is documented in docs/design/01-data-model.md, and a reader that knows only
// that document can recover every record - which is what makes the log an artefact rather than a
// row range.
//
// Not a replacement for SQLite (still the default): segments exist for deployments that want to
// copy, ship and inspect a log, and for retention, where dropping a prefix means deleting files
// instead of leaving freelist holes in a table.
//
// Contract: openspec/specs/store/segment-log-store and store/log-store.

#include <mosaic/store/log_store.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace mosaic {

class SegmentLogStore final : public LogStore {
public:
    /// Open or create the store rooted at `root`. Empty when the directory cannot be used or
    /// `HEAD` declares a format this build does not know - refusing loudly beats parsing
    /// optimistically.
    static std::optional<std::unique_ptr<SegmentLogStore>>
    open(const std::string& root, Durability durability = Durability::PerRecord,
         std::uint64_t segment_bytes = 8u << 20);

    ~SegmentLogStore() override;
    SegmentLogStore(const SegmentLogStore&) = delete;
    SegmentLogStore& operator=(const SegmentLogStore&) = delete;

    AppendResult append(const Record& record) override;
    AppendResult append_batch(const std::vector<Record>& records) override;

    std::optional<Seq> head_seq(const LogKey& key) const override;
    std::optional<Record> get(const LogKey& key, Seq seq) const override;
    std::vector<Record> read(const LogKey& key, Seq from, std::size_t count) const override;
    std::vector<LogKey> keys() const override;

    /// A checkpoint is kept beside the log as `<channel>/checkpoint.bin`, never as a segment, so
    /// the segment scan cannot mistake it for history.
    bool put_checkpoint(const LogKey& key, const Bytes& blob) override;
    std::optional<Bytes> checkpoint(const LogKey& key) const override;
    /// Drops whole segments that end before `first_kept`, which is why a checkpoint advance lands
    /// on a segment boundary. A segment that straddles the boundary is kept: rewriting a segment is
    /// the one thing an append-only layout will not do.
    std::optional<std::size_t> truncate_before(const LogKey& key, Seq first_kept) override;

    Durability durability() const;

    /// Bytes of a torn final record discarded when this store was opened, across every log. A crash
    /// that cost something should be visible rather than silent.
    std::size_t recovered_bytes() const;

    /// The format `HEAD` declares, for a test or an operator reading the layout.
    static constexpr std::uint8_t kFormatVersion = 1;

private:
    struct Impl;
    explicit SegmentLogStore(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

}  // namespace mosaic
