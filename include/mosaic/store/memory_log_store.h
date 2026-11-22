#pragma once

#include <mosaic/store/log_store.h>

#include <cstddef>
#include <map>
#include <optional>

namespace mosaic {

/// Reference LogStore: RAM only, used by tests and the simulator. Read this first when writing a
/// new backend.
class MemoryLogStore final : public LogStore, public ForkEvidenceStore {
public:
    AppendResult append(const Record& record) override;
    std::optional<Seq> head_seq(const LogKey& key) const override;
    std::optional<Record> get(const LogKey& key, Seq seq) const override;
    std::vector<Record> read(const LogKey& key, Seq from, std::size_t count) const override;
    std::vector<LogKey> keys() const override;

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
    std::map<LogKey, std::vector<Record>> logs_;
    std::map<LogKey, Bytes> checkpoints_;
    std::map<LogKey, std::map<Seq, ForkEvidence>> forks_;
};

}  // namespace mosaic
