#pragma once

#include <compare>
#include <mosaic/core/crypto.h>
#include <mosaic/core/types.h>
#include <string>

namespace mosaic {

/// Identifies one log: a single writer's append-only sequence on one channel.
struct LogKey {
    WriterId writer;
    Channel channel;
    friend auto operator<=>(const LogKey&, const LogKey&) = default;
};

/// One entry in a log. `hash = H(canonical_bytes(record))`, `sig = Sign(hash)`.
/// Record N commits to record N-1 through `prev`, so a log is a hash chain.
struct Record {
    WriterId writer;
    Channel channel;
    Seq seq = 0;
    Hash prev {}; // hash of record seq-1; all-zero when seq == 0
    std::uint64_t timestamp_ms = 0;
    std::string content_type; // e.g. "application/json"; opaque to Mosaic
    Bytes payload;
    Bytes sig; // NOT part of canonical_bytes

    LogKey key() const { return { writer, channel }; }
};

/// Summary of a log: how far it goes and the hash of its last record.
struct Head {
    Seq seq = 0;
    Hash hash {};
};

/// Canonical, unambiguous encoding of every field except `sig` (format in docs/design/01-data-model.md).
Bytes canonical_bytes(const Record& record);

Hash record_hash(const Record& record, const Hasher& hasher);

} // namespace mosaic
