#include <algorithm>
#include <mosaic/store/memory_log_store.h>

namespace mosaic {

AppendResult MemoryLogStore::append(const Record& record)
{
    auto& log = logs_[record.key()];
    if (record.seq != log.size())
        return AppendResult::NotNext;
    log.push_back(record);
    return AppendResult::Ok;
}

std::optional<Seq> MemoryLogStore::head_seq(const LogKey& key) const
{
    auto it = logs_.find(key);
    if (it == logs_.end() || it->second.empty())
        return std::nullopt;
    return it->second.size() - 1;
}

std::optional<Record> MemoryLogStore::get(const LogKey& key, Seq seq) const
{
    auto it = logs_.find(key);
    if (it == logs_.end() || seq >= it->second.size())
        return std::nullopt;
    return it->second[seq];
}

std::vector<Record> MemoryLogStore::read(const LogKey& key, Seq from, std::size_t count) const
{
    auto it = logs_.find(key);
    if (it == logs_.end() || from >= it->second.size())
        return { };
    const auto first = it->second.begin() + static_cast<std::ptrdiff_t>(from);
    const auto available = static_cast<std::size_t>(it->second.end() - first);
    return std::vector<Record>(first, first + static_cast<std::ptrdiff_t>(std::min(count, available)));
}

std::vector<LogKey> MemoryLogStore::keys() const
{
    std::vector<LogKey> out;
    for (const auto& [key, log] : logs_)
        if (!log.empty())
            out.push_back(key);
    return out;
}

} // namespace mosaic
