#pragma once

// Binary encoding of every message and of records on the wire (ENC-1/ENC-2). Contract:
// openspec/specs/proto/wire-format/spec.md.
//
// Frame layout:   u32 payload_size | u8 message_id | payload
//   payload_size counts the id byte and the payload, not the length field itself.
// Record layout:  canonical_bytes(record) | u32 sig_size | sig
//   so a receiver can recompute the hash from the bytes it actually received.
//
// Message ids are permanent: never renumber, never reuse. Ids for messages that exist but are not
// implemented yet are already allocated here, so implementing one later is not a wire break.

#include <mosaic/core/record.h>
#include <mosaic/proto/messages.h>

#include <cstdint>
#include <optional>

namespace mosaic {

// Implemented today.
inline constexpr std::uint8_t kMsgRecord = 1;
inline constexpr std::uint8_t kMsgHead = 2;
inline constexpr std::uint8_t kMsgFetch = 3;
inline constexpr std::uint8_t kMsgDirectRecord = 4;
// Reserved now, implemented later (HELLO-1 version half is live; pattern advertisement is not).
inline constexpr std::uint8_t kMsgHello = 5;
inline constexpr std::uint8_t kMsgAck = 6;
inline constexpr std::uint8_t kMsgCredit = 7;
inline constexpr std::uint8_t kMsgGetHeads = 8;
inline constexpr std::uint8_t kMsgDirectHead = 9;
inline constexpr std::uint8_t kMsgPeers = 10;
/// Everything at or above this is reserved for experiments; a receiver ignores it.
inline constexpr std::uint8_t kMsgExperimental = 128;

/// Most peer entries one PeersMsg may carry. A peer list is a hint, so a message that claims more
/// than this is refused whole rather than partly believed (DISC-3).
inline constexpr std::uint32_t kMaxPeersEntries = 64;
/// Oldest age a peer entry may claim. A frame-level sanity bound, not the entry's lifetime: the
/// node expires entries on its own configured clock.
inline constexpr std::uint64_t kMaxPeerEntryAgeMs = 4ull * 60 * 60 * 1000;

/// Hard limits, checked before any allocation happens (hostile input is expected).
inline constexpr std::uint32_t kMaxFrameBytes = 1u << 20;   // 1 MiB
inline constexpr std::uint32_t kMaxRecordBytes = 1u << 18;  // 256 KiB

enum class DecodeStatus {
    Ok,
    Truncated,  // the frame promises more bytes than it has
    Oversized,  // declared length above kMaxFrameBytes
    UnknownId,  // an id this build does not implement: ignore and count, do not fail the link
    Malformed,  // decoded fields do not re-encode identically, or trailing bytes remain
    UnsupportedVersion,  // a hello announcing a protocol version we cannot speak
    TooManyEntries,      // a peer list above kMaxPeersEntries
};

const char* to_string(DecodeStatus status);

struct DecodeResult {
    DecodeStatus status = DecodeStatus::Ok;
    std::optional<Message> message;

    bool ok() const {
        return status == DecodeStatus::Ok && message.has_value();
    }
};

/// Is this a peer endpoint this protocol accepts: "tcp://host:port" with a port in range? Exposed
/// so a peer list that arrives as a value (a simulator, a test) is held to the same rule the
/// decoder applies to bytes.
bool valid_peer_endpoint(const std::string& endpoint);

/// A message's permanent id.
std::uint8_t message_id(const Message& message);

/// Full frame, ready for a link.
Bytes encode_message(const Message& message);

/// Total function over arbitrary bytes: either a message or a status, never a crash and never a
/// partially decoded value.
DecodeResult decode_message(const Bytes& frame);

/// Record on the wire: canonical bytes plus a length-prefixed signature.
Bytes encode_record(const Record& record);
std::optional<Record> decode_record(const Bytes& bytes);

}  // namespace mosaic
