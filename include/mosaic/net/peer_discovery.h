#pragma once

// Where a node learns about peers it was not told about (spec: discovery/peer-discovery).
//
// Two shapes here, deliberately:
//   - PeerDiscovery: an interface whose implementations *produce candidates*. The daemon loop pulls
//   from it
//     once per iteration and never shares state with it, which is what lets the czmq actors own
//     their own threads without putting a lock anywhere near the node.
//   - PeerBook: the bookkeeping every source needs - dial a peer once, bound the set, forget a peer
//   that
//     stops announcing. Pulled out because it is the part worth testing, and it has no I/O at all.

#include <mosaic/core/types.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace mosaic {

struct DiscoveredPeer {
    NodeId id;
    std::string endpoint;  // host:port of the peer's PUB
    std::string curve_public;
};

/// A source of candidate peers. `poll_discovered` MUST be non-blocking and MUST NOT block the
/// caller on a thread it does not own: the daemon calls it once per loop iteration.
class PeerDiscovery {
public:
    virtual ~PeerDiscovery() = default;

    /// Candidates learned since the last call.
    virtual std::vector<DiscoveredPeer> poll_discovered() = 0;

    /// Names this source for the log, e.g. "static", "beacon", "gossip".
    virtual const char* kind() const = 0;
};

/// The configured peer list, expressed through the same interface, so a deployment with no
/// discovery runs through one code path rather than a special case.
class StaticPeerDiscovery final : public PeerDiscovery {
public:
    explicit StaticPeerDiscovery(std::vector<DiscoveredPeer> peers) : peers_(std::move(peers)) {}

    std::vector<DiscoveredPeer> poll_discovered() override;
    const char* kind() const override {
        return "static";
    }

private:
    std::vector<DiscoveredPeer> peers_;
    bool delivered_ = false;
};

/// What a node does with candidates: dial each peer once, keep the set bounded, and notice peers
/// that go quiet before forgetting them.
///
/// Two thresholds, taken from zyre's peer model: a peer that misses announcements is *evasive*
/// (maybe busy, maybe on a bad link) and one that stays silent is *expired* (gone). One threshold
/// cannot tell an overloaded peer from a dead one, and the difference is exactly what an operator
/// needs.
class PeerBook {
public:
    explicit PeerBook(std::size_t max_tracked = 64, std::uint64_t evasive_ms = 10000,
                      std::uint64_t expired_ms = 30000)
        : max_tracked_(max_tracked), evasive_ms_(evasive_ms),
          expired_ms_(expired_ms > evasive_ms ? expired_ms : evasive_ms) {}

    /// Record a sighting. True when this is a peer we should dial now (first time we have seen it).
    bool observe(const DiscoveredPeer& peer, std::uint64_t now_ms);

    /// Peers that went quiet since the last call but are not gone yet. Reported once each, so a
    /// caller can note it without repeating itself on every loop iteration.
    std::vector<NodeId> take_evasive(std::uint64_t now_ms);

    /// Drop peers not seen within the expired window. Returns how many went away.
    std::size_t expire(std::uint64_t now_ms);

    std::size_t size() const {
        return peers_.size();
    }
    std::size_t overflow() const {
        return overflow_;
    }  // sightings dropped because the book was full
    bool knows(const NodeId& id) const {
        return peers_.count(id) != 0;
    }
    std::vector<DiscoveredPeer> peers() const;

private:
    struct Entry {
        DiscoveredPeer peer;
        std::uint64_t last_seen_ms = 0;
        bool dialled = false;
        bool evasive = false;  // already reported as quiet; reset by the next announcement
    };

    std::size_t max_tracked_;
    std::uint64_t evasive_ms_;
    std::uint64_t expired_ms_;
    std::size_t overflow_ = 0;
    std::map<NodeId, Entry> peers_;
};

}  // namespace mosaic
