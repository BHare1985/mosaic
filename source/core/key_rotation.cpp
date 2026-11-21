#include <mosaic/core/encoding.h>
#include <mosaic/core/key_rotation.h>

#include <cstring>

using std::nullopt;
using std::optional;
using std::uint8_t;

namespace mosaic {
namespace {
constexpr uint8_t kRotationFormat = 1;
constexpr size_t kKeyBytes = 32;
constexpr size_t kMaxPayloadBytes = 64;
}  // namespace

Bytes encode_rotation_link(const RotationLink& link) {
    Bytes out;
    ByteWriter writer(out);
    writer.put_u8(kRotationFormat);
    writer.put_u8(static_cast<uint8_t>(link.hash_algo));
    const auto key = from_hex(link.successor);
    if (key && key->size() == kKeyBytes) {
        writer.put_bytes(key->data(), key->size());
    } else {
        // An unusable successor is encoded as a zero-length key, which decodes to nothing: a caller
        // that passed a malformed key gets a link that resolves to nothing rather than a payload
        // that is half of one.
        writer.put_bytes(nullptr, 0);
    }
    writer.put_u64(link.successor_start_seq);
    return out;
}

optional<RotationLink> decode_rotation_link(const Bytes& payload) {
    if (payload.size() > kMaxPayloadBytes || payload.empty()) {
        return nullopt;  // bounded before anything is allocated
    }
    ByteReader reader(payload);
    if (reader.get_u8() != kRotationFormat) {
        return nullopt;  // a format this build does not know is refused, not guessed at
    }
    const auto algorithm = reader.get_u8();
    if (algorithm < static_cast<uint8_t>(HashAlgo::Sha256) ||
        algorithm > static_cast<uint8_t>(HashAlgo::Blake2b)) {
        return nullopt;
    }

    RotationLink link;
    link.hash_algo = static_cast<HashAlgo>(algorithm);
    // Fixed-width key: read it as a length-prefixed blob would be, but check the size we asked for.
    const auto key = reader.get_bytes();
    if (key.size() != kKeyBytes) {
        return nullopt;  // a successor key is exactly one Ed25519 public key
    }
    link.successor = to_hex(key);
    link.successor_start_seq = reader.get_u64();
    if (!reader.ok() || !reader.at_end()) {
        return nullopt;
    }
    if (key == Bytes(kKeyBytes, 0)) {
        return nullopt;  // a zeroed key is a placeholder, never a successor
    }
    return link;
}

}  // namespace mosaic
