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

/// Version 2 added `HelloMsg::spare_capacity`, so a hello from a version 1 peer cannot be told from
/// one that advertised no patterns - the version number is how the decoder refuses to guess
/// (HELLO-1, JOBS-4).
inline constexpr std::uint8_t kProtocolVersion = 2;

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

/// Replica -> writer: "record `seq` and everything before it is durably stored here". The floor a
/// writer's retention policy cannot go below (ACK-1).
struct AckMsg {
    LogKey key;
    Seq seq = 0;
};

/// Newcomer -> peers: "tell me the heads of every log matching these patterns" - how a log is found
/// when its writer is offline (HEADS-1).
struct GetHeadsMsg {
    std::vector<std::string> patterns;
};

/// Reply to GetHeadsMsg, unicast (HEADS-1).
struct DirectHeadMsg {
    LogKey key;
    Seq seq = 0;
    Hash hash{};
};

/// Explicit credit grant for streaming paths: live delivery paced by the receiver (FLOW-3).
struct CreditMsg {
    std::uint32_t credits = 0;
};

/// First message on a new link: protocol version, what the sender follows, and how much spare work
/// it could take on. Both advertisements are hints: nothing in the delivery or acceptance path may
/// read them (spec: proto/peer-advertisement).
struct HelloMsg {
    std::uint8_t version = kProtocolVersion;
    std::vector<std::string> patterns;  // what the sender follows, bounded by max_head_patterns
    /// Coarse spare capacity, 0..255. Zero means "prefer someone else", never "unreachable" - one
    /// byte on purpose, so nobody can argue about the precision of a hint.
    std::uint8_t spare_capacity = 0;
};

/// One peer a sender knows about (DISC-3). `hops` is how far the entry has travelled from its
/// origin; `age_ms` is how long the sender has known it. Durations, never timestamps: clocks are
/// not comparable across nodes and nobody should pretend they are.
struct PeerEntry {
    NodeId id;
    std::string pub_endpoint;     // where that node's PUB listens, e.g. "tcp://10.0.0.2:5555"
    std::string router_endpoint;  // where that node's ROUTER listens
    std::uint32_t hops = 0;
    std::uint64_t age_ms = 0;
};

/// Peer-list gossip: "here is who I know" (DISC-3). A hint, never authority: an entry grants
/// nothing until the usual handshake and pin policy accept the link.
struct PeersMsg {
    std::vector<PeerEntry> entries;
};

using Message = std::variant<RecordMsg, DirectRecordMsg, HeadMsg, FetchMsg, AckMsg, GetHeadsMsg,
                             DirectHeadMsg, CreditMsg, HelloMsg, PeersMsg>;

}  // namespace mosaic
