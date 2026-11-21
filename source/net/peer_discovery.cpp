#include <mosaic/net/peer_discovery.h>

using std::move;
using std::size_t;
using std::uint64_t;
using std::vector;

namespace mosaic {

vector<DiscoveredPeer> StaticPeerDiscovery::poll_discovered() {
    if (delivered_) {
        return {};
    }
    delivered_ = true;
    return peers_;
}

bool PeerBook::observe(const DiscoveredPeer& peer, uint64_t now_ms) {
    auto it = peers_.find(peer.id);
    if (it != peers_.end()) {
        it->second.last_seen_ms = now_ms;
        it->second.evasive = false;  // it is talking again
        return false;                // already known: refresh, do not dial again
    }
    if (peers_.size() >= max_tracked_) {
        ++overflow_;  // bounded: one host announcing identities cannot grow this without limit
        return false;
    }
    Entry entry;
    entry.peer = peer;
    entry.last_seen_ms = now_ms;
    entry.dialled = true;
    peers_.emplace(peer.id, std::move(entry));
    return true;
}

vector<NodeId> PeerBook::take_evasive(uint64_t now_ms) {
    vector<NodeId> quiet;
    for (auto& [id, entry] : peers_) {
        if (entry.evasive || now_ms - entry.last_seen_ms < evasive_ms_) {
            continue;
        }
        entry.evasive = true;
        quiet.push_back(id);
    }
    return quiet;
}

size_t PeerBook::expire(uint64_t now_ms) {
    size_t dropped = 0;
    for (auto it = peers_.begin(); it != peers_.end();) {
        if (now_ms - it->second.last_seen_ms >= expired_ms_) {
            it = peers_.erase(it);
            ++dropped;
        } else {
            ++it;
        }
    }
    return dropped;
}

vector<DiscoveredPeer> PeerBook::peers() const {
    vector<DiscoveredPeer> out;
    out.reserve(peers_.size());
    for (const auto& [id, entry] : peers_) {
        out.push_back(entry.peer);
    }
    return out;
}

}  // namespace mosaic
