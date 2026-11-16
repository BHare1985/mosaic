#pragma once

// Byte-level primitives shared by the record and message codecs (ENC-1). All integers are big-endian
// and every variable-length field is a u32 length prefix, which is what makes the encodings
// unambiguous: a reader can never be shifted onto another field's bytes.
//
// The reader is a *checked* reader: any short read poisons it, and the caller only has to look at
// `ok()` once, so a forgotten bounds check cannot become an overread. The writer is deliberately
// dumb; there is nothing to get wrong on that side.
//
// Wire format and rationale: openspec/specs/proto/wire-format/spec.md, docs/design/02-protocol.md.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mosaic/core/types.h>
#include <string>
#include <string_view>

namespace mosaic {

class ByteWriter {
public:
    explicit ByteWriter(Bytes& out)
        : out_(out)
    {
    }

    void put_u8(std::uint8_t value) { out_.push_back(value); }

    void put_u32(std::uint32_t value)
    {
        for (int shift = 24; shift >= 0; shift -= 8)
            out_.push_back(static_cast<std::uint8_t>(value >> shift));
    }

    void put_u64(std::uint64_t value)
    {
        for (int shift = 56; shift >= 0; shift -= 8)
            out_.push_back(static_cast<std::uint8_t>(value >> shift));
    }

    void put_raw(const std::uint8_t* data, std::size_t size)
    {
        out_.insert(out_.end(), data, data + size);
    }

    void put_hash(const Hash& hash) { put_raw(hash.bytes.data(), hash.bytes.size()); }

    /// Length-prefixed byte string.
    void put_bytes(const Bytes& bytes) { put_bytes(bytes.data(), bytes.size()); }

    void put_bytes(const std::uint8_t* data, std::size_t size)
    {
        put_u32(static_cast<std::uint32_t>(size));
        put_raw(data, size);
    }

    /// Length-prefixed text (not NUL-terminated on the wire).
    void put_string(std::string_view text) { put_bytes(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()); }

private:
    Bytes& out_;
};

/// Bounds-checked reader. After any failed read, `ok()` is false and every later read returns a
/// default value, so a decoder can read its fields in sequence and check once at the end.
class ByteReader {
public:
    explicit ByteReader(const Bytes& bytes)
        : data_(bytes.data())
        , size_(bytes.size())
    {
    }

    bool ok() const { return ok_; }
    /// Bytes consumed so far, so a decoder can slice the exact region a value occupied.
    std::size_t pos() const { return pos_; }
    bool at_end() const { return ok_ && pos_ == size_; }
    std::size_t remaining() const { return ok_ ? size_ - pos_ : 0; }

    std::uint8_t get_u8()
    {
        const auto* p = take(1);
        return p ? p[0] : 0;
    }

    std::uint32_t get_u32()
    {
        const auto* p = take(4);
        if (!p)
            return 0;
        return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16)
            | (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
    }

    std::uint64_t get_u64()
    {
        const auto hi = get_u64_impl();
        return hi;
    }

    Hash get_hash()
    {
        Hash hash;
        const auto* p = take(hash.bytes.size());
        if (p)
            std::memcpy(hash.bytes.data(), p, hash.bytes.size());
        return hash;
    }

    Bytes get_bytes()
    {
        const auto size = get_u32();
        const auto* p = take(size);
        return p ? Bytes(p, p + size) : Bytes { };
    }

    std::string get_string()
    {
        const auto size = get_u32();
        const auto* p = take(size);
        return p ? std::string(reinterpret_cast<const char*>(p), size) : std::string { };
    }

    /// Skip `count` bytes; fails if they are not there. Used to bound-check a declared length
    /// before allocating, so a hostile length cannot cause a large allocation.
    bool skip(std::uint32_t count) { return take(count) != nullptr; }

private:
    const std::uint8_t* take(std::size_t count)
    {
        if (!ok_ || count > size_ - pos_) {
            ok_ = false;
            return nullptr;
        }
        const auto* p = data_ + pos_;
        pos_ += count;
        return p;
    }

    std::uint64_t get_u64_impl()
    {
        const auto* p = take(8);
        if (!p)
            return 0;
        std::uint64_t value = 0;
        for (int i = 0; i < 8; ++i)
            value = (value << 8) | p[i];
        return value;
    }

    const std::uint8_t* data_;
    std::size_t size_;
    std::size_t pos_ = 0;
    bool ok_ = true;
};

} // namespace mosaic
