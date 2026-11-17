#pragma once

#include <map>
#include <mosaic/store/log_store.h>

namespace mosaic {

/// Reference LogStore: RAM only, used by tests and the simulator. Read this first when writing a new backend.
class MemoryLogStore final : public LogStore, public ForkEvidenceStore {
public:
    AppendResult append(const Record& record) override;
    std::optional<Seq> head_seq(const LogKey& key) const override;
    std::optional<Record> get(const LogKey& key, Seq seq) const override;
    std::vector<Record> read(const LogKey& key, Seq from, std::size_t count) const override;
    std::vector<LogKey> keys() const override;

    ForkEvidenceStore* evidence() override { return this; }
    const ForkEvidenceStore* evidence() const override { return this; }
    bool save_fork(const ForkEvidence& evidence) override;
    std::vector<ForkEvidence> load_forks(const LogKey& key) const override;

private:
    std::map<LogKey, std::vector<Record>> logs_;
    std::map<LogKey, std::map<Seq, ForkEvidence>> forks_;
};

} // namespace mosaic
