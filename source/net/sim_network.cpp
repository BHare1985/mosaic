#include <mosaic/net/sim_network.h>

namespace mosaic {

void SimNetwork::deliver(const NodeId& from, const NodeId& to, const Message& message)
{
    auto it = peers_.find(to);
    if (it == peers_.end())
        return;
    std::uniform_real_distribution<double> coin(0.0, 1.0);
    if (coin(rng_) < loss_) {
        ++dropped_;
        return;
    }
    it->second.inbox.push_back({ from, message });
    ++delivered_;
    if (coin(rng_) < duplicate_) {
        it->second.inbox.push_back({ from, message });
        ++delivered_;
    }
}

SimTransport::SimTransport(SimNetwork& net, NodeId id)
    : net_(net)
    , id_(std::move(id))
{
    net_.add_peer(id_);
}

void SimTransport::subscribe(const std::string& pattern) { net_.peers_[id_].subs.add(pattern); }

void SimTransport::publish(const Channel& channel, Message message)
{
    for (auto& [peer_id, peer] : net_.peers_)
        if (peer_id != id_ && peer.subs.matches_any(channel))
            net_.deliver(id_, peer_id, message);
}

void SimTransport::send(const NodeId& to, Message message) { net_.deliver(id_, to, message); }

std::optional<Envelope> SimTransport::poll()
{
    auto& inbox = net_.peers_[id_].inbox;
    if (inbox.empty())
        return std::nullopt;
    Envelope e = std::move(inbox.front());
    inbox.pop_front();
    return e;
}

} // namespace mosaic
