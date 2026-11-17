#pragma once

#include <compare>
#include <mosaic/core/crypto.h>
#include <mosaic/core/types.h>
#include <optional>
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
    /// Which hash produced `prev`/this record's hash. Part of the canonical bytes, so a log written
    /// under one algorithm stays verifiable after the default changes.
    HashAlgo hash_algo = kDefaultHashAlgo;
    Seq seq = 0;
    Hash prev { }; // hash of record seq-1; all-zero when seq == 0
    std::uint64_t timestamp_ms = 0;
    std::string content_type; // e.g. "application/json"; opaque to Mosaic
    Bytes payload;
    Bytes sig; // NOT part of canonical_bytes

    LogKey key() const { return { writer, channel }; }

    /// Value equality, so a failing comparison in a test shows what differs rather than "a != b".
    friend bool operator==(const Record&, const Record&) = default;
};

/// Summary of a log: how far it goes and the hash of its last record.
struct Head {
    Seq seq = 0;
    Hash hash { };
};

/// Canonical, unambiguous encoding of every field except `sig` (format in docs/design/01-data-model.md).
Bytes canonical_bytes(const Record& record);

/// Hash with a caller-chosen hasher (writers, tests). The tag in the record is NOT used here.
Hash record_hash(const Record& record, const Hasher& hasher);

/// Hash using the algorithm the record's own tag names. Empty when this build cannot compute that
/// algorithm, so a verifier rejects the record instead of comparing against zeros.
std::optional<Hash> record_hash_tagged(const Record& record);

} // namespace mosaic
