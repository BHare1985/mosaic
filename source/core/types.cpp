#include <mosaic/core/types.h>

namespace mosaic {

std::string to_hex(const Hash& hash)
{
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (auto byte : hash.bytes) {
        out.push_back(digits[byte >> 4]);
        out.push_back(digits[byte & 0x0f]);
    }
    return out;
}

} // namespace mosaic
