#include <mosaic/core/encoding.h>
#include <mosaic/log/checkpoint.h>

#include <cstdint>

using std::nullopt;
using std::optional;
using std::uint8_t;

namespace mosaic {
namespace {
constexpr uint8_t kCheckpointFormat = 1;
}  // namespace

Bytes checkpoint_canonical(const Checkpoint& checkpoint) {
    Bytes out;
    ByteWriter writer(out);
    writer.put_u8(kCheckpointFormat);
    writer.put_u8(static_cast<uint8_t>(checkpoint.hash_algo));
    writer.put_string(checkpoint.key.writer);
    writer.put_string(checkpoint.key.channel);
    writer.put_u64(checkpoint.seq);
    writer.put_hash(checkpoint.hash);
    return out;
}

optional<Hash> checkpoint_digest(const Checkpoint& checkpoint) {
    // The algorithm is named in the checkpoint, so a history written under a previous default stays
    // verifiable - the same rule records follow.
    return hash_with(checkpoint.hash_algo, checkpoint_canonical(checkpoint));
}

Bytes encode_checkpoint(const Checkpoint& checkpoint) {
    Bytes out = checkpoint_canonical(checkpoint);
    ByteWriter tail(out);
    tail.put_bytes(checkpoint.sig);
    return out;
}

optional<Checkpoint> decode_checkpoint(const Bytes& bytes) {
    ByteReader reader(bytes);
    if (reader.get_u8() != kCheckpointFormat)
        return nullopt;  // a format this build does not know is refused, not guessed at

    Checkpoint checkpoint;
    const auto algorithm = reader.get_u8();
    if (algorithm < static_cast<uint8_t>(HashAlgo::Sha256) ||
        algorithm > static_cast<uint8_t>(HashAlgo::Blake2b))
        return nullopt;
    checkpoint.hash_algo = static_cast<HashAlgo>(algorithm);
    checkpoint.key.writer = reader.get_string();
    checkpoint.key.channel = reader.get_string();
    checkpoint.seq = reader.get_u64();
    checkpoint.hash = reader.get_hash();
    checkpoint.sig = reader.get_bytes();
    if (!reader.ok() || !reader.at_end())
        return nullopt;

    // One canonical form, or none: a value that does not re-encode to the bytes it came from is not
    // the checkpoint those bytes claim to be.
    if (encode_checkpoint(checkpoint) != bytes)
        return nullopt;
    return checkpoint;
}

Checkpoint sign_checkpoint(Checkpoint checkpoint, const Signer& signer) {
    if (const auto digest = checkpoint_digest(checkpoint))
        checkpoint.sig = signer.sign(*digest);
    return checkpoint;
}

bool verify_checkpoint(const Checkpoint& checkpoint, const Verifier& verifier) {
    const auto digest = checkpoint_digest(checkpoint);
    if (!digest)
        return false;  // an algorithm this build cannot compute is indistinguishable from a forgery
    return verifier.verify(checkpoint.key.writer, *digest, checkpoint.sig);
}

}  // namespace mosaic
