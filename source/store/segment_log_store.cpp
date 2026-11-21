#include <mosaic/proto/codec.h>
#include <mosaic/store/segment_log_store.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <system_error>
#include <unistd.h>
#include <vector>

using std::make_unique;
using std::map;
using std::move;
using std::nullopt;
using std::optional;
using std::size_t;
using std::stoul;
using std::stoull;
using std::string;
using std::uint32_t;
using std::uint64_t;
using std::uint8_t;
using std::unique_ptr;
using std::vector;

namespace mosaic {
namespace {

namespace fs = std::filesystem;

constexpr const char* kHeadFile = "HEAD";
/// "MOSAICSEG": a store that does not start with these bytes is not ours, and is refused rather
/// than parsed.
constexpr const char* kMagic = "MOSAICSEG";
constexpr size_t kMagicBytes = 9;
/// Frame layout: u32 length | u32 crc | u64 seq | payload.
/// `length` counts everything after itself (crc + seq + payload), so a reader can validate a frame
/// from its first twelve bytes, and a torn tail is a frame that promises more than the file holds.
constexpr size_t kFrameHeader = 16;
constexpr size_t kFrameOverhead = 12;  // length field excluded

uint32_t crc32(const uint8_t* data, size_t size) {
    // Bitwise CRC-32, no table: the store is not in the per-record hot path often enough for a
    // table to matter, and a table is 1 KiB of cache for a check with no alternatives to argue
    // about.
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

/// A channel is a log's name and also a directory. Keep the readable characters (a channel like
/// events.a should look like itself on disk) and escape everything else as %XX.
string escape(const string& text) {
    string out;
    out.reserve(text.size());
    for (const char c : text) {
        const auto ch = static_cast<unsigned char>(c);
        const bool readable = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                              (ch >= '0' && ch <= '9') || ch == '.' || ch == '_' || ch == '-';
        if (readable) {
            out.push_back(c);
            continue;
        }
        static const char* digits = "0123456789abcdef";
        out.push_back('%');
        out.push_back(digits[ch >> 4]);
        out.push_back(digits[ch & 0x0F]);
    }
    return out;
}

string unescape(const string& text) {
    string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size()) {
            const auto hex = text.substr(i + 1, 2);
            out.push_back(static_cast<char>(std::stoul(hex, nullptr, 16)));
            i += 2;
            continue;
        }
        out.push_back(text[i]);
    }
    return out;
}

string segment_name(Seq first_seq) {
    char buffer[32] = {};
    std::snprintf(buffer, sizeof(buffer), "%020llu", static_cast<unsigned long long>(first_seq));
    return string(buffer) + ".seg";
}

string index_name(Seq first_seq) {
    auto name = segment_name(first_seq);
    name.replace(name.size() - 4, 4, ".idx");
    return name;
}

optional<Seq> segment_first_seq(const string& file_name) {
    if (file_name.size() != 24 || file_name.substr(20) != ".seg")
        return nullopt;
    for (size_t i = 0; i < 20; ++i)
        if (file_name[i] < '0' || file_name[i] > '9')
            return nullopt;
    return static_cast<Seq>(std::stoull(file_name.substr(0, 20)));
}

/// One record's place in a segment: enough to read it back without scanning.
struct IndexEntry {
    uint32_t offset = 0;  // from the start of the segment
    uint32_t length = 0;  // frame length, header included
    Hash hash{};          // the record's hash, so a reader can check what it got
};

/// A segment file and its derived index. The segment is authoritative; the index is what we guessed
/// last time, and is rebuilt from the segment when the two disagree.
struct Segment {
    Seq first_seq = 0;
    fs::path path;
    fs::path index_path;
    uint64_t size = 0;
    vector<IndexEntry> entries;
};

struct LogState {
    Seq head = 0;
    bool has_records = false;
    vector<Segment> segments;
    /// The last segment is the only one that may be appended to, and only until it is full.
    int write_fd = -1;
};

uint32_t read_u32(const string& buffer, size_t offset) {
    uint32_t value = 0;
    std::memcpy(&value, buffer.data() + offset, sizeof(value));
    return value;
}

void put_u32(string& buffer, uint32_t value) {
    const auto at = buffer.size();
    buffer.resize(at + sizeof(value));
    std::memcpy(buffer.data() + at, &value, sizeof(value));
}

void put_u64(string& buffer, uint64_t value) {
    const auto at = buffer.size();
    buffer.resize(at + sizeof(value));
    std::memcpy(buffer.data() + at, &value, sizeof(value));
}

/// Read a whole file. A missing file is empty, not an error: the store is expected to be incomplete
/// only in the ways it can recover from.
string slurp(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return {};
    return string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

bool spit(const fs::path& path, const string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        return false;
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return out.good();
}

}  // namespace

struct SegmentLogStore::Impl {
    fs::path root;
    Durability durability = Durability::PerRecord;
    uint64_t segment_bytes = 8u << 20;
    size_t recovered = 0;
    mutable map<LogKey, LogState> logs;
    map<LogKey, bool> loaded;

    fs::path log_dir(const LogKey& key) const {
        return root / key.writer / escape(key.channel);
    }

    bool write_head() const {
        string head;
        head.append(kMagic, kMagicBytes);
        head.push_back(static_cast<char>(kFormatVersion));
        return spit(root / kHeadFile, head);
    }

    /// Scan one segment, truncating a torn final record and rebuilding its index. Returns the
    /// number of complete records, so the caller can derive the head.
    size_t scan(Segment& segment) {
        string bytes = slurp(segment.path);
        size_t offset = 0;
        size_t complete = 0;
        segment.entries.clear();
        while (offset + kFrameHeader <= bytes.size()) {
            const auto frame = read_u32(bytes, offset);
            if (frame < kFrameOverhead || frame > kMaxFrameBytes)
                break;
            if (offset + 4 + frame > bytes.size())
                break;  // torn tail: the frame promised more than the file has
            const auto expected =
                crc32(reinterpret_cast<const uint8_t*>(bytes.data()) + offset + 8, frame - 4);
            if (expected != read_u32(bytes, offset + 4))
                break;  // a damaged frame ends the log here rather than being guessed at
            IndexEntry entry;
            entry.offset = static_cast<uint32_t>(offset);
            entry.length = frame + 4;
            const auto payload = bytes.substr(offset + kFrameHeader, frame - kFrameOverhead);
            const auto record = decode_record(Bytes(payload.begin(), payload.end()));
            if (!record)
                break;
            const auto hash = record_hash_tagged(*record);
            if (!hash)
                break;
            entry.hash = *hash;
            segment.entries.push_back(entry);
            offset += entry.length;
            ++complete;
        }
        if (offset < bytes.size()) {
            // Everything from `offset` on is a torn tail: discard it, so the next append starts at
            // a frame boundary the reader can trust.
            recovered += bytes.size() - offset;
            std::error_code ignored;
            fs::resize_file(segment.path, offset, ignored);
        }
        segment.size = offset;
        return complete;
    }

    void rebuild_index(const Segment& segment) const {
        string bytes;
        for (const auto& entry : segment.entries) {
            string frame;
            put_u32(frame, entry.offset);
            put_u32(frame, entry.length);
            frame.append(reinterpret_cast<const char*>(entry.hash.bytes.data()), 32);
            bytes += frame;
        }
        spit(segment.index_path, bytes);
    }

    bool load(const LogKey& key) {
        if (loaded[key])
            return true;
        LogState state;
        const auto dir = log_dir(key);
        std::error_code ignored;
        if (!fs::exists(dir, ignored)) {
            loaded[key] = true;
            logs[key] = move(state);
            return true;
        }
        for (const auto& item : fs::directory_iterator(dir, ignored)) {
            const auto name = item.path().filename().string();
            const auto first = segment_first_seq(name);
            if (!first)
                continue;
            Segment segment;
            segment.first_seq = *first;
            segment.path = item.path();
            segment.index_path = dir / index_name(*first);
            const auto complete = scan(segment);
            rebuild_index(
                segment);  // derived state: written from the segment, never trusted over it
            if (complete != 0) {
                state.head = segment.first_seq + complete - 1;
                state.has_records = true;
            }
            state.segments.push_back(move(segment));
        }
        std::sort(state.segments.begin(), state.segments.end(),
                  [](const Segment& a, const Segment& b) { return a.first_seq < b.first_seq; });
        logs[key] = move(state);
        loaded[key] = true;
        return true;
    }

    vector<LogKey> all_keys() const {
        vector<LogKey> out;
        std::error_code error;
        for (const auto& writer : fs::directory_iterator(root, error)) {
            if (!writer.is_directory())
                continue;
            for (const auto& channel : fs::directory_iterator(writer.path(), error)) {
                if (!channel.is_directory())
                    continue;
                out.push_back(LogKey{writer.path().filename().string(),
                                     unescape(channel.path().filename().string())});
            }
        }
        return out;
    }

    bool write_frame(int fd, Seq seq, const Bytes& payload, size_t& written) {
        string body;  // what the crc covers: seq, then the record's bytes
        put_u64(body, seq);
        body.append(reinterpret_cast<const char*>(payload.data()), payload.size());

        string frame;
        // The length counts the crc as well as the body: a reader validates a frame from its first
        // twelve bytes, so the length has to describe everything that follows the length field.
        put_u32(frame, static_cast<uint32_t>(body.size() + 4));
        put_u32(frame, crc32(reinterpret_cast<const uint8_t*>(body.data()), body.size()));
        frame += body;
        written = frame.size();
        return ::write(fd, frame.data(), frame.size()) == static_cast<ssize_t>(frame.size());
    }
};

SegmentLogStore::SegmentLogStore(std::unique_ptr<Impl> impl) : impl_(move(impl)) {}

SegmentLogStore::~SegmentLogStore() {
    for (auto& [key, state] : impl_->logs)
        if (state.write_fd >= 0)
            ::close(state.write_fd);
}

optional<std::unique_ptr<SegmentLogStore>>
SegmentLogStore::open(const string& root, Durability durability, uint64_t segment_bytes) {
    std::error_code error;
    fs::create_directories(root, error);
    if (error)
        return nullopt;

    const auto head_path = fs::path(root) / kHeadFile;
    if (fs::exists(head_path, error)) {
        const string head = slurp(head_path);
        if (head.size() < kMagicBytes + 1 || head.compare(0, kMagicBytes, kMagic) != 0)
            return nullopt;  // not ours
        if (static_cast<uint8_t>(head[kMagicBytes]) != kFormatVersion)
            return nullopt;  // a newer format is refused rather than parsed optimistically
    }

    auto impl = make_unique<Impl>();
    impl->root = root;
    impl->durability = durability;
    impl->segment_bytes = segment_bytes == 0 ? (8u << 20) : segment_bytes;
    auto store = unique_ptr<SegmentLogStore>(new SegmentLogStore(move(impl)));
    if (!store->impl_->write_head())
        return nullopt;
    for (const auto& key : store->impl_->all_keys())
        store->impl_->load(
            key);  // recovery on open, so a torn tail is discarded before anything appends
    return store;
}

Durability SegmentLogStore::durability() const {
    return impl_->durability;
}

size_t SegmentLogStore::recovered_bytes() const {
    return impl_->recovered;
}

AppendResult SegmentLogStore::append(const Record& record) {
    if (!impl_->load(record.key()))
        return AppendResult::NotNext;
    auto& state = impl_->logs[record.key()];
    const Seq next = state.has_records ? state.head + 1 : 0;
    if (record.seq != next)
        return AppendResult::NotNext;

    const auto payload = encode_record(record);
    const auto dir = impl_->log_dir(record.key());
    std::error_code error;
    fs::create_directories(dir, error);

    // Roll when the current segment is full, so a segment is never rewritten once it is full.
    if (state.segments.empty() ||
        state.segments.back().size + kFrameHeader + payload.size() > impl_->segment_bytes) {
        Segment segment;
        segment.first_seq = record.seq;
        segment.path = dir / segment_name(record.seq);
        segment.index_path = dir / index_name(record.seq);
        state.segments.push_back(move(segment));
    }

    const auto& segment = state.segments.back();
    const int fd = ::open(segment.path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0)
        return AppendResult::NotNext;

    size_t written = 0;
    const bool ok = impl_->write_frame(fd, record.seq, payload, written);
    if (ok && impl_->durability == Durability::PerRecord)
        ::fsync(fd);
    ::close(fd);
    if (!ok)
        return AppendResult::NotNext;

    auto& mutable_segment = impl_->logs[record.key()].segments.back();
    IndexEntry entry;
    entry.offset = static_cast<uint32_t>(mutable_segment.size);
    entry.length = static_cast<uint32_t>(written);
    if (const auto hash = record_hash_tagged(record))
        entry.hash = *hash;
    mutable_segment.entries.push_back(entry);
    mutable_segment.size += written;
    impl_->rebuild_index(mutable_segment);

    auto& updated = impl_->logs[record.key()];
    updated.head = record.seq;
    updated.has_records = true;
    return AppendResult::Ok;
}

AppendResult SegmentLogStore::append_batch(const vector<Record>& records) {
    for (const auto& record : records) {
        const auto result = append(record);
        if (result != AppendResult::Ok)
            return result;
    }
    return AppendResult::Ok;
}

optional<Seq> SegmentLogStore::head_seq(const LogKey& key) const {
    if (!impl_->load(key))
        return nullopt;
    const auto& state = impl_->logs[key];
    if (!state.has_records)
        return nullopt;
    return state.head;
}

optional<Record> SegmentLogStore::get(const LogKey& key, Seq seq) const {
    if (!impl_->load(key))
        return nullopt;
    for (const auto& segment : impl_->logs[key].segments) {
        if (segment.entries.empty())
            continue;
        const Seq last = segment.first_seq + segment.entries.size() - 1;
        if (seq < segment.first_seq || seq > last)
            continue;
        const auto& entry = segment.entries[static_cast<size_t>(seq - segment.first_seq)];
        // Bounds are checked against the file, not trusted from the index: a damaged segment must
        // report the record missing rather than hand back bytes it cannot vouch for.
        std::error_code error;
        if (static_cast<uint64_t>(entry.offset) + entry.length > fs::file_size(segment.path, error))
            return nullopt;
        std::ifstream in(segment.path, std::ios::binary);
        if (!in)
            return nullopt;
        in.seekg(entry.offset);
        string frame(entry.length, '\0');
        in.read(frame.data(), static_cast<std::streamsize>(frame.size()));
        if (!in)
            return nullopt;
        const auto payload = frame.substr(16);
        return decode_record(Bytes(payload.begin(), payload.end()));
    }
    return nullopt;
}

vector<Record> SegmentLogStore::read(const LogKey& key, Seq from, size_t count) const {
    vector<Record> out;
    for (size_t i = 0; i < count; ++i) {
        auto record = get(key, from + i);
        if (!record)
            break;
        out.push_back(move(*record));
    }
    return out;
}

bool SegmentLogStore::put_checkpoint(const LogKey& key, const Bytes& blob) {
    if (!impl_->load(key))
        return false;
    std::error_code error;
    fs::create_directories(impl_->log_dir(key), error);
    return spit(impl_->log_dir(key) / "checkpoint.bin", string(blob.begin(), blob.end()));
}

optional<Bytes> SegmentLogStore::checkpoint(const LogKey& key) const {
    const auto bytes = slurp(impl_->log_dir(key) / "checkpoint.bin");
    if (bytes.empty())
        return nullopt;
    return Bytes(bytes.begin(), bytes.end());
}

optional<size_t> SegmentLogStore::truncate_before(const LogKey& key, Seq first_kept) {
    if (!impl_->load(key))
        return nullopt;
    auto& state = impl_->logs[key];
    size_t dropped = 0;
    // Whole segments only, and never the last one: a log whose history is entirely dropped would
    // have no head to start from, and the checkpoint already says what happened up to there.
    while (state.segments.size() > 1) {
        const auto& front = state.segments.front();
        const Seq last = front.first_seq + (front.entries.empty() ? 0 : front.entries.size() - 1);
        if (!front.entries.empty() && last >= first_kept)
            break;  // this segment straddles the boundary: keep it rather than rewrite it
        dropped += front.entries.size();
        std::error_code error;
        fs::remove(front.path, error);
        fs::remove(front.index_path, error);
        state.segments.erase(state.segments.begin());
    }
    return dropped;
}

vector<LogKey> SegmentLogStore::keys() const {
    auto out = impl_->all_keys();
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace mosaic
