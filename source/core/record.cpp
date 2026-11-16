#include <mosaic/core/encoding.h>
#include <mosaic/core/record.h>

namespace mosaic {

// Layout (all integers big-endian, every variable-length field is u32-length-prefixed):
//   u8 format=1 | u8 hash_algo | writer | channel | u64 seq | 32 bytes prev | u64 timestamp_ms |
//   content_type | payload
//
// The algorithm tag is inside the hashed bytes on purpose: a verifier reads the algorithm from the
// bytes it already holds instead of from a build setting (see proto/wire-format spec).
Bytes canonical_bytes(const Record& r)
{
    Bytes out;
    ByteWriter writer(out);
    writer.put_u8(1); // record format version
    writer.put_u8(static_cast<std::uint8_t>(r.hash_algo));
    writer.put_string(r.writer);
    writer.put_string(r.channel);
    writer.put_u64(r.seq);
    writer.put_hash(r.prev);
    writer.put_u64(r.timestamp_ms);
    writer.put_string(r.content_type);
    writer.put_bytes(r.payload);
    return out;
}

Hash record_hash(const Record& record, const Hasher& hasher) { return hasher.hash(canonical_bytes(record)); }

std::optional<Hash> record_hash_tagged(const Record& record) { return hash_with(record.hash_algo, canonical_bytes(record)); }

} // namespace mosaic
