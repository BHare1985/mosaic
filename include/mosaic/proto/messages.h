#pragma once

// Wire-level vocabulary (transport independent). Semantics, grammar and state machines:
// docs/design/02-protocol.md. Binary encoding (ENC-*) is not implemented yet: today messages move
// as C++ values through SimNetwork. When the encoder lands, every message here needs a round-trip
// test.

#include <mosaic/core/record.h>

#include <string>
#include <variant>
#include <vector>

namespace mosaic {

inline constexpr std::uint8_t kProtocolVersion = 1;

/// Live delivery of a new record (writer -> everyone following the channel). Best effort.
struct RecordMsg {
    Record record;
};

/// Repair delivery of a requested record (responder -> requester, unicast).
struct DirectRecordMsg {
    Record record;
};

/// Periodic "my log ends here" announcement (writer -> channel followers). Lets followers notice
/// gaps.
struct HeadMsg {
    LogKey key;
    Seq seq = 0;
    Hash hash{};
};

/// Request `count` records of a log starting at `from`. `count` is also the requester's credit:
/// the responder must never send more than that.
struct FetchMsg {
    LogKey key;
    Seq from = 0;
    std::uint32_t count = 0;
};

/// Replica -> writer: "record `seq` and everything before it is durably stored here". (ACK-1, not
/// yet handled.)
struct AckMsg {
    LogKey key;
    Seq seq = 0;
};

/// Newcomer -> peers: "tell me the heads of every log matching these patterns". (HEADS-1, not yet
/// handled.)
struct GetHeadsMsg {
    std::vector<std::string> patterns;
};

/// Reply to GetHeadsMsg, unicast. (HEADS-1, not yet handled.)
struct DirectHeadMsg {
    LogKey key;
    Seq seq = 0;
    Hash hash{};
};

/// Explicit credit grant for streaming paths. (FLOW-3, not yet handled.)
struct CreditMsg {
    std::uint32_t credits = 0;
};

/// First message on a new link: protocol version and what the sender follows. (HELLO-1, not yet
/// handled.)
struct HelloMsg {
    std::uint8_t version = kProtocolVersion;
    std::vector<std::string> patterns;
};

using Message = std::variant<RecordMsg, DirectRecordMsg, HeadMsg, FetchMsg, AckMsg, GetHeadsMsg,
                             DirectHeadMsg, CreditMsg, HelloMsg>;

}  // namespace mosaic
