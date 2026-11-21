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
    // Sequence-based, not position-based: a log may start after sequence 0 once retention has
    // dropped a prefix, and "next" has to mean the same thing either way (STORE-3).
    const Seq next = log.empty() ? 0 : log.back().seq + 1;
    if (record.seq != next) {
        return AppendResult::NotNext;
    }
    log.push_back(record);
    return AppendResult::Ok;
}

optional<Seq> MemoryLogStore::head_seq(const LogKey& key) const {
    auto it = logs_.find(key);
    if (it == logs_.end() || it->second.empty()) {
        return nullopt;
    }
    return it->second.back().seq;
}

optional<Record> MemoryLogStore::get(const LogKey& key, Seq seq) const {
    auto it = logs_.find(key);
    if (it == logs_.end() || it->second.empty()) {
        return nullopt;
    }
    const auto& log = it->second;
    if (seq < log.front().seq || seq > log.back().seq) {
        return nullopt;
    }
    return log[static_cast<size_t>(seq - log.front().seq)];
}

vector<Record> MemoryLogStore::read(const LogKey& key, Seq from, size_t count) const {
    auto it = logs_.find(key);
    if (it == logs_.end() || it->second.empty()) {
        return {};
    }
    const auto& log = it->second;
    if (from < log.front().seq || from > log.back().seq) {
        return {};
    }
    const auto first = log.begin() + static_cast<ptrdiff_t>(from - log.front().seq);
    const auto available = static_cast<size_t>(log.end() - first);
    return {first, first + static_cast<ptrdiff_t>(min(count, available))};
}

vector<LogKey> MemoryLogStore::keys() const {
    vector<LogKey> out;
    for (const auto& [key, log] : logs_) {
        if (!log.empty()) {
            out.push_back(key);
        }
    }
    return out;
}

AppendResult LogStore::append_batch(const vector<Record>& records) {
    for (const auto& record : records) {
        const auto result = append(record);
        if (result != AppendResult::Ok) {
            return result;  // same rule as one at a time: the first non-next record stops the batch
        }
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
    if (it == forks_.end()) {
        return out;
    }
    for (const auto& [seq, evidence] : it->second) {
        out.push_back(evidence);
    }
    return out;
}

bool MemoryLogStore::put_checkpoint(const LogKey& key, const Bytes& blob) {
    checkpoints_[key] = blob;
    return true;
}

std::optional<Bytes> MemoryLogStore::checkpoint(const LogKey& key) const {
    const auto it = checkpoints_.find(key);
    if (it == checkpoints_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::optional<std::size_t> MemoryLogStore::truncate_before(const LogKey& key, Seq first_kept) {
    auto it = logs_.find(key);
    if (it == logs_.end()) {
        return 0;
    }
    auto& records = it->second;
    std::size_t dropped = 0;
    while (!records.empty() && records.front().seq < first_kept) {
        records.erase(records.begin());
        ++dropped;
    }
    return dropped;
}

}  // namespace mosaic
