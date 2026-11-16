#pragma once

// Crypto seams. Everything above this header talks to Hasher / Signer / Verifier and never to a
// concrete algorithm, so the real implementations (libsodium: BLAKE2b or SHA-256, Ed25519) can be
// swapped in without touching log, node or tests. See docs/design/07-security.md.

#include <mosaic/core/types.h>
#include <utility>

namespace mosaic {

/// SHA-256 (FIPS 180-4), dependency-free reference implementation.
Hash sha256(const std::uint8_t* data, std::size_t size);
Hash sha256(const Bytes& data);

class Hasher {
public:
    virtual ~Hasher() = default;
    virtual Hash hash(const Bytes& data) const = 0;
};

class Sha256Hasher final : public Hasher {
public:
    Hash hash(const Bytes& data) const override { return sha256(data); }
};

class Signer {
public:
    virtual ~Signer() = default;
    /// Identity that appears as Record::writer.
    virtual WriterId id() const = 0;
    virtual Bytes sign(const Hash& record_hash) const = 0;
};

class Verifier {
public:
    virtual ~Verifier() = default;
    virtual bool verify(const WriterId& writer, const Hash& record_hash, const Bytes& signature) const = 0;
};

/// !!! NOT SECURE. Deterministic stand-in so the rest of the system can be built and tested before
/// Ed25519 lands (TODO CORE-2). "Signature" = SHA-256("INSECURE-TEST|" + id + hash); anyone who
/// knows the id can forge it. Never use outside tests and demos.
class InsecureTestSigner final : public Signer, public Verifier {
public:
    explicit InsecureTestSigner(WriterId id)
        : id_(std::move(id))
    {
    }
    WriterId id() const override { return id_; }
    Bytes sign(const Hash& record_hash) const override { return make(id_, record_hash); }
    bool verify(const WriterId& writer, const Hash& record_hash, const Bytes& signature) const override
    {
        return make(writer, record_hash) == signature;
    }

private:
    static Bytes make(const WriterId& id, const Hash& record_hash);
    WriterId id_;
};

} // namespace mosaic
