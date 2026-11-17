#include <mosaic/core/types.h>

using std::nullopt;
using std::optional;
using std::size_t;
using std::string;
using std::string_view;
using std::uint8_t;

namespace mosaic {

string to_hex(const Hash& hash) {
    static constexpr char digits[] = "0123456789abcdef";
    string out;
    out.reserve(64);
    for (auto byte : hash.bytes) {
        out.push_back(digits[byte >> 4]);
        out.push_back(digits[byte & 0x0f]);
    }
    return out;
}

string to_hex(const Bytes& bytes) {
    static constexpr char digits[] = "0123456789abcdef";
    string out;
    out.reserve(bytes.size() * 2);
    for (const auto byte : bytes) {
        out.push_back(digits[byte >> 4]);
        out.push_back(digits[byte & 0x0f]);
    }
    return out;
}

optional<Bytes> from_hex(string_view text) {
    const auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    };
    if (text.size() % 2 != 0)
        return nullopt;
    Bytes out;
    out.reserve(text.size() / 2);
    for (size_t i = 0; i < text.size(); i += 2) {
        const int hi = nibble(text[i]);
        const int lo = nibble(text[i + 1]);
        if (hi < 0 || lo < 0)
            return nullopt;
        out.push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
    return out;
}

}  // namespace mosaic
