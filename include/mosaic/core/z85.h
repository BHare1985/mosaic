#pragma once

// Z85 (ZeroMQ RFC 32): the text encoding ZeroMQ uses for CURVE keys. 4 binary bytes -> 5
// characters. Implemented here rather than pulled from a dependency, because the output must match
// libzmq's byte-for-byte and the alphabet is stable and tiny.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace mosaic {

/// Encode a multiple of 4 bytes. Empty when the length is not a multiple of 4.
std::string z85_encode(const std::uint8_t* data, std::size_t size);

/// Decode text whose length is a multiple of 5. Empty when it is not, or a character is not in the
/// alphabet - a malformed key must be rejected, never guessed at.
std::optional<std::string> z85_decode(std::string_view text);

}  // namespace mosaic
