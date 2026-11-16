#include <mosaic/core/record.h>

namespace mosaic {
namespace {
    void put_u32(Bytes& out, std::uint32_t v)
    {
        for (int i = 3; i >= 0; --i)
            out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    }
    void put_u64(Bytes& out, std::uint64_t v)
    {
        for (int i = 7; i >= 0; --i)
            out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    }
    void put_bytes(Bytes& out, const std::uint8_t* data, std::size_t size)
    {
        put_u32(out, static_cast<std::uint32_t>(size));
        out.insert(out.end(), data, data + size);
    }
    void put_string(Bytes& out, const std::string& s)
    {
        put_bytes(out, reinterpret_cast<const std::uint8_t*>(s.data()), s.size());
    }
} // namespace

// Layout (all integers big-endian, every variable-length field is u32-length-prefixed):
//   u8 format=1 | writer | channel | u64 seq | 32 bytes prev | u64 timestamp_ms | content_type | payload
Bytes canonical_bytes(const Record& r)
{
    Bytes out(1, 1); // format version (note: reserve()+push_back() trips a GCC 15 false positive here)
    put_string(out, r.writer);
    put_string(out, r.channel);
    put_u64(out, r.seq);
    out.insert(out.end(), r.prev.bytes.begin(), r.prev.bytes.end());
    put_u64(out, r.timestamp_ms);
    put_string(out, r.content_type);
    put_bytes(out, r.payload.data(), r.payload.size());
    return out;
}

Hash record_hash(const Record& record, const Hasher& hasher) { return hasher.hash(canonical_bytes(record)); }

} // namespace mosaic
