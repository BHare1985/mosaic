#pragma once

// Basic vocabulary types shared by every module. Keep this header tiny and dependency-free.

#include <array>
#include <cstdint>
#include <string>
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
    std::array<std::uint8_t, 32> bytes {};
    friend bool operator==(const Hash&, const Hash&) = default;
};

std::string to_hex(const Hash& hash);

} // namespace mosaic
