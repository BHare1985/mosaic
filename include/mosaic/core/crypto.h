#pragma once

// Crypto seams. Everything above this header talks to Hasher / Signer / Verifier and never to a
// concrete algorithm, so implementations can be swapped without touching log, node or tests.
// Threat model and layers: docs/design/07-security.md.
//
// Two algorithms exist and both are tagged on the wire (HashAlgo), so a log written under one stays
// verifiable after the default changes. Records are signed with Ed25519 over the record hash; the
// writer identity is the hex public key, which is content-derived and survives restarts.

#include <mosaic/core/types.h>

#include <cstdint>
#include <optional>
#include <utility>

namespace mosaic {

/// Hash algorithm stamped into every record. These values are on the wire: never renumber.
enum class HashAlgo : std::uint8_t {
    Sha256 = 1,
    Blake2b = 2,
};

/// What a writer stamps when it is not told otherwise. BLAKE2b when the build has libsodium, so a
/// plain `Mosaic` build keeps working with the dependency-free reference implementation.
#ifdef MOSAIC_HAS_SODIUM
inline constexpr HashAlgo kDefaultHashAlgo = HashAlgo::Blake2b;
#else
inline constexpr HashAlgo kDefaultHashAlgo = HashAlgo::Sha256;
#endif

/// SHA-256 (FIPS 180-4), dependency-free reference implementation.
Hash sha256(const std::uint8_t* data, std::size_t size);
Hash sha256(const Bytes& data);

/// Does this build know how to compute `algo`?
bool hash_algo_supported(HashAlgo algo);

/// Algorithm-tagged hashing. Empty when the algorithm is unknown to this build, so a verifier can
/// reject a record instead of comparing against zeros.
std::optional<Hash> hash_with(HashAlgo algo, const Bytes& data);

class Hasher {
public:
    virtual ~Hasher() = default;
    virtual Hash hash(const Bytes& data) const = 0;
    /// Tag stamped into records this writer creates.
    virtual HashAlgo algorithm() const = 0;
};

class Sha256Hasher final : public Hasher {
public:
    Hash hash(const Bytes& data) const override {
        return sha256(data);
    }
    HashAlgo algorithm() const override {
        return HashAlgo::Sha256;
    }
};

class Signer {
public:
    virtual ~Signer() = default;
    /// Identity that appears as Record::writer (hex public key for real keys).
    virtual WriterId id() const = 0;
    virtual Bytes sign(const Hash& record_hash) const = 0;
};

class Verifier {
public:
    virtual ~Verifier() = default;
    virtual bool verify(const WriterId& writer, const Hash& record_hash,
                        const Bytes& signature) const = 0;
};

#ifdef MOSAIC_HAS_SODIUM

class Blake2bHasher final : public Hasher {
public:
    Hash hash(const Bytes& data) const override;
    HashAlgo algorithm() const override {
        return HashAlgo::Blake2b;
    }
};

/// Ed25519 signing over the record hash. `id()` is the hex public key, so two writers with
/// different keys can never share an identity and a restart keeps the same one.
class Ed25519Signer final : public Signer {
public:
    /// `public_key` is 32 bytes, `secret_key` is libsodium's 64-byte form.
    Ed25519Signer(Bytes public_key, Bytes secret_key);
    WriterId id() const override {
        return to_hex(public_key_);
    }
    Bytes sign(const Hash& record_hash) const override;
    const Bytes& public_key() const {
        return public_key_;
    }

private:
    Bytes public_key_;
    Bytes secret_key_;
};

class Ed25519Verifier final : public Verifier {
public:
    bool verify(const WriterId& writer, const Hash& record_hash,
                const Bytes& signature) const override;
};

/// Generate a fresh Ed25519 key pair (32-byte public, 64-byte secret).
std::optional<std::pair<Bytes, Bytes>> ed25519_generate();

#endif  // MOSAIC_HAS_SODIUM

}  // namespace mosaic
