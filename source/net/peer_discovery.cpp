#include <mosaic/net/peer_discovery.h>

namespace mosaic {

std::vector<DiscoveredPeer> StaticPeerDiscovery::poll_discovered()
{
    if (delivered_)
        return { };
    delivered_ = true;
    return peers_;
}

bool PeerBook::observe(const DiscoveredPeer& peer, std::uint64_t now_ms)
{
    auto it = peers_.find(peer.id);
    if (it != peers_.end()) {
        it->second.last_seen_ms = now_ms;
        it->second.evasive = false; // it is talking again
        return false; // already known: refresh, do not dial again
    }
    if (peers_.size() >= max_tracked_) {
        ++overflow_; // bounded: one host announcing identities cannot grow this without limit
        return false;
    }
    Entry entry;
    entry.peer = peer;
    entry.last_seen_ms = now_ms;
    entry.dialled = true;
    peers_.emplace(peer.id, std::move(entry));
    return true;
}

std::vector<NodeId> PeerBook::take_evasive(std::uint64_t now_ms)
{
    std::vector<NodeId> quiet;
    for (auto& [id, entry] : peers_) {
        if (entry.evasive || now_ms - entry.last_seen_ms < evasive_ms_)
            continue;
        entry.evasive = true;
        quiet.push_back(id);
    }
    return quiet;
}

std::size_t PeerBook::expire(std::uint64_t now_ms)
{
    std::size_t dropped = 0;
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

std::vector<DiscoveredPeer> PeerBook::peers() const
{
    std::vector<DiscoveredPeer> out;
    for (const auto& [id, entry] : peers_)
        out.push_back(entry.peer);
    return out;
}

} // namespace mosaic
