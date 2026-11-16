#pragma once

// Basic vocabulary types shared by every module. Keep this header tiny and dependency-free.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mosaic {

using Bytes = std::vector<std::uint8_t>;

/// Position of a record inside one log. The first record of a log has Seq 0.
using Seq = std::uint64_t;

/// Network-level address of a peer (an endpoint name, not an identity).
using NodeId = std::string;

/// Identity of a log writer: the (hex-encoded) public key. Opaque to everything except crypto.h.
using WriterId = std::string;

/// Dotted channel name, e.g. "storage.eu.shard7". See match/matcher.h for the pattern language.
using Channel = std::string;

/// 256-bit hash. All-zero means "no previous record" (used as prev of Seq 0).
struct Hash {
    std::array<std::uint8_t, 32> bytes { };
    friend bool operator==(const Hash&, const Hash&) = default;
};

std::string to_hex(const Hash& hash);

/// Lowercase hex, two characters per byte. Used for keys and hashes in text form.
std::string to_hex(const Bytes& bytes);

/// Parse lowercase or uppercase hex. Empty when the text is not an even-length hex string, so a
/// malformed identity is rejected instead of silently becoming an empty key.
std::optional<Bytes> from_hex(std::string_view text);

} // namespace mosaic
