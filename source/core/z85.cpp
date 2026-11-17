#include <mosaic/core/z85.h>

namespace mosaic {
namespace {
    constexpr char kAlphabet[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.-:+=^!/*?&<>()[]{}@%$#";
    constexpr std::size_t kAlphabetSize = 85;

    int char_value(char c)
    {
        for (std::size_t i = 0; i < kAlphabetSize; ++i)
            if (kAlphabet[i] == c)
                return static_cast<int>(i);
        return -1;
    }
} // namespace

std::string z85_encode(const std::uint8_t* data, std::size_t size)
{
    if (size == 0 || size % 4 != 0)
        return { };
    std::string out;
    out.reserve(size / 4 * 5);
    for (std::size_t offset = 0; offset < size; offset += 4) {
        std::uint32_t value = 0;
        for (int i = 0; i < 4; ++i)
            value = (value << 8) | data[offset + static_cast<std::size_t>(i)];
        char chunk[5];
        for (int i = 4; i >= 0; --i) {
            chunk[i] = kAlphabet[value % kAlphabetSize];
            value /= kAlphabetSize;
        }
        out.append(chunk, 5);
    }
    return out;
}

std::optional<std::string> z85_decode(std::string_view text)
{
    if (text.empty() || text.size() % 5 != 0)
        return std::nullopt;
    std::string out;
    out.reserve(text.size() / 5 * 4);
    for (std::size_t offset = 0; offset < text.size(); offset += 5) {
        std::uint64_t value = 0;
        for (std::size_t i = 0; i < 5; ++i) {
            const int digit = char_value(text[offset + i]);
            if (digit < 0)
                return std::nullopt;
            value = value * kAlphabetSize + static_cast<std::uint64_t>(digit);
        }
        if (value > 0xffffffffull)
            return std::nullopt; // out of range for the 4 bytes it must produce
        for (int i = 3; i >= 0; --i)
            out.push_back(static_cast<char>((value >> (8 * i)) & 0xff));
    }
    return out;
}

} // namespace mosaic
