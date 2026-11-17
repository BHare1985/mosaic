#pragma once

// Writer key material on disk. The identity is *derived* from the public key, never stored
// separately, so it cannot drift from the key that actually signs (CORE-2).
//
// Rules: the file is created readable and writable only by its owner, and it is written through a
// temporary file plus a rename so a crash cannot leave a half-written key. Key material must never
// be put in configuration files, logs or error messages - see docs/design/07-security.md.

#include <mosaic/core/crypto.h>
#include <mosaic/core/types.h>
#include <optional>
#include <string>

namespace mosaic {

struct Keystore {
    Bytes public_key; // 32 bytes (Ed25519 public key)
    Bytes secret_key; // 64 bytes (libsodium's signing secret key)

    /// Hex public key: the value that appears as Record::writer.
    WriterId id() const { return to_hex(public_key); }
};

/// Fresh Ed25519 key pair. Empty when the build has no libsodium.
std::optional<Keystore> generate_keystore();

/// Write with mode 0600 via a temporary file and a rename. False on any I/O or permission failure.
bool save_keystore(const Keystore& keystore, const std::string& path);

/// Load and validate sizes. Empty when the file is missing, unreadable or malformed.
std::optional<Keystore> load_keystore(const std::string& path);

/// The same identity in the form ZeroMQ's CURVE wants: a curve25519 key pair, z85-encoded (40 chars
/// each). Derived from the Ed25519 secret, so the key that authenticates a link is the key that signs
/// records - see docs/design/07-security.md, "One identity, two representations".
struct CurveIdentity {
    std::string public_z85;
    std::string secret_z85;
};

std::optional<CurveIdentity> curve_identity_from_keystore(const Keystore& keystore);

} // namespace mosaic
