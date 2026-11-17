#pragma once

// The certificate container the ZeroMQ ecosystem (czmq, malamute tooling) uses for CURVE keys.
// Support it as *import/export only*: our identity of record is the Ed25519 keystore, and these
// files are how a node interoperates with tools that speak that format. See
// docs/design/07-security.md.

#include <optional>
#include <string>

namespace mosaic {

struct ZcertIdentity {
    std::string public_z85;  // curve25519 public key, 40 z85 characters
    std::string secret_z85;  // curve25519 secret key, 40 z85 characters
    std::string name;        // optional metadata, kept so a round trip does not lose it
    std::string email;
    std::string organization;
};

/// Read a certificate file. Empty when it is missing, unreadable, or does not contain a usable key
/// pair.
std::optional<ZcertIdentity> load_zcert(const std::string& path);

/// Write a certificate file in the ecosystem's format, readable by that tooling.
bool save_zcert(const ZcertIdentity& identity, const std::string& path);

}  // namespace mosaic
