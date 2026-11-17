#include <mosaic/store/memory_log_store.h>

#include <algorithm>

using std::min;
using std::nullopt;
using std::optional;
using std::ptrdiff_t;
using std::size_t;
using std::vector;

namespace mosaic {

AppendResult MemoryLogStore::append(const Record& record) {
    auto& log = logs_[record.key()];
    if (record.seq != log.size())
        return AppendResult::NotNext;
    log.push_back(record);
    return AppendResult::Ok;
}

optional<Seq> MemoryLogStore::head_seq(const LogKey& key) const {
    auto it = logs_.find(key);
    if (it == logs_.end() || it->second.empty())
        return nullopt;
    return it->second.size() - 1;
}

optional<Record> MemoryLogStore::get(const LogKey& key, Seq seq) const {
    auto it = logs_.find(key);
    if (it == logs_.end() || seq >= it->second.size())
        return nullopt;
    return it->second[seq];
}

vector<Record> MemoryLogStore::read(const LogKey& key, Seq from, size_t count) const {
    auto it = logs_.find(key);
    if (it == logs_.end() || from >= it->second.size())
        return {};
    const auto first = it->second.begin() + static_cast<ptrdiff_t>(from);
    const auto available = static_cast<size_t>(it->second.end() - first);
    return vector<Record>(first, first + static_cast<ptrdiff_t>(min(count, available)));
}

vector<LogKey> MemoryLogStore::keys() const {
    vector<LogKey> out;
    for (const auto& [key, log] : logs_)
        if (!log.empty())
            out.push_back(key);
    return out;
}

AppendResult LogStore::append_batch(const vector<Record>& records) {
    for (const auto& record : records) {
        const auto result = append(record);
        if (result != AppendResult::Ok)
            return result;  // same rule as one at a time: the first non-next record stops the batch
    }
    return AppendResult::Ok;
}

bool MemoryLogStore::save_fork(const ForkEvidence& evidence) {
    forks_[evidence.key][evidence.seq] = evidence;  // same log and sequence: idempotent
    return true;
}

vector<ForkEvidence> MemoryLogStore::load_forks(const LogKey& key) const {
    vector<ForkEvidence> out;
    const auto it = forks_.find(key);
    if (it == forks_.end())
        return out;
    for (const auto& [seq, evidence] : it->second)
        out.push_back(evidence);
    return out;
}

}  // namespace mosaic
