#pragma once

// A rotation link: the old key, still trusted, signs a statement naming its successor (SEC-3). The
// link is an ordinary record in the old log - hash-chained, signed, replicated, proved offline by
// `Mosaic verify` - and its payload is this. Everything about the payload is opaque to the log
// layer, which is what lets a replica that has never heard of rotation store and forward it.
//
// Payload: `u8 format | u8 algorithm | u8 successor[32] | u64 successor_start_seq`.
// Documented in docs/design/01-data-model.md. Contract: openspec/specs/core/key-rotation.

#include <mosaic/core/crypto.h>
#include <mosaic/core/types.h>

#include <cstdint>
#include <optional>

namespace mosaic {

/// The content type that marks a record as a rotation link.
inline constexpr const char* kRotationContentType = "mosaic/rotation";

struct RotationLink {
    WriterId successor;                     // the key taking over, as a hex public key
    Seq successor_start_seq = 0;            // the first sequence in the successor's log
    HashAlgo hash_algo = kDefaultHashAlgo;  // the algorithm that log's records will be tagged with
};

Bytes encode_rotation_link(const RotationLink& link);

/// Total over arbitrary bytes: a truncated, oversized or malformed payload yields nothing, never a
/// half-read successor.
std::optional<RotationLink> decode_rotation_link(const Bytes& payload);

}  // namespace mosaic
