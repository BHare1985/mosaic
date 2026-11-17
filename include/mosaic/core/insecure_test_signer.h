#pragma once

// !!! NOT SECURITY. Kept in its own header so nothing in the product can reach it by accident: only
// tests include this file, and CORE-2 (Ed25519) is what the product uses. See
// docs/design/07-security.md.

#include <mosaic/core/crypto.h>

#include <utility>

namespace mosaic {

/// Deterministic stand-in for a real signature: "signature" = SHA-256("INSECURE-TEST|" + id +
/// hash). Anyone who knows the id can forge it, which is fine for checking plumbing and useless for
/// authenticity. Never use outside tests and demos.
class InsecureTestSigner final : public Signer, public Verifier {
public:
    explicit InsecureTestSigner(WriterId id) : id_(std::move(id)) {}
    WriterId id() const override {
        return id_;
    }
    Bytes sign(const Hash& record_hash) const override {
        return make(id_, record_hash);
    }
    bool verify(const WriterId& writer, const Hash& record_hash,
                const Bytes& signature) const override {
        return make(writer, record_hash) == signature;
    }

private:
    static Bytes make(const WriterId& id, const Hash& record_hash);
    WriterId id_;
};

}  // namespace mosaic
