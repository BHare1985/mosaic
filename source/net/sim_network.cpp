#include <mosaic/net/sim_network.h>

#include <algorithm>

using std::make_pair;
using std::move;
using std::mt19937_64;
using std::nullopt;
using std::optional;
using std::string;
using std::uint32_t;
using std::uint64_t;
using std::uniform_int_distribution;
using std::uniform_real_distribution;
using std::vector;

namespace mosaic {
namespace {
// FNV-1a over the two identities, finalised with splitmix64 and mixed with the run seed. Every link
// direction therefore gets its own stream: adding a peer cannot perturb the draws of an existing
// link, which is what keeps "re-running the reported seed reproduces the failure" true across
// edits.
uint64_t link_seed(uint64_t seed, const NodeId& from, const NodeId& to) {
    uint64_t h = 1469598103934665603ULL;
    for (const auto* id : {&from, &to}) {
        for (const char c : *id) {
            h ^= static_cast<unsigned char>(c);
            h *= 1099511628211ULL;
        }
    }
    h += seed + 0x9e3779b97f4a7c15ULL;
    h = (h ^ (h >> 30)) * 0xbf58476d1ce4e5b9ULL;
    h = (h ^ (h >> 27)) * 0x94d049bb133111ebULL;
    return h ^ (h >> 31);
}
}  // namespace

SimNetwork::SimNetwork(uint64_t seed) : seed_(seed) {}

void SimNetwork::set_loss(double probability) {
    defaults_.loss = probability;
    for (auto& [key, state] : links_) {
        state.config.loss = probability;
    }
}

void SimNetwork::set_duplicate(double probability) {
    defaults_.duplicate = probability;
    for (auto& [key, state] : links_) {
        state.config.duplicate = probability;
    }
}

void SimNetwork::set_delay(uint32_t min_ms, uint32_t max_ms) {
    defaults_.delay_min_ms = min_ms;
    defaults_.delay_max_ms = max_ms > min_ms ? max_ms : min_ms;
    for (auto& [key, state] : links_) {
        state.config.delay_min_ms = min_ms;
        state.config.delay_max_ms = defaults_.delay_max_ms;
    }
}

void SimNetwork::set_link(const NodeId& from, const NodeId& to, LinkConfig config) {
    link(from, to).config = config;
}

void SimNetwork::partition(const NodeId& a, const NodeId& b) {
    blocked_.emplace(a, b);
    blocked_.emplace(b, a);
}

void SimNetwork::heal(const NodeId& a, const NodeId& b) {
    blocked_.erase({a, b});
    blocked_.erase({b, a});
}

bool SimNetwork::partitioned(const NodeId& a, const NodeId& b) const {
    return blocked_.contains({a, b});
}

void SimNetwork::inject_undecodable(const NodeId& from, const NodeId& to) {
    if (from == to) {
        return;  // the contract forbids self-delivery, faults included
    }
    const auto peer = peers_.find(to);
    if (peer == peers_.end()) {
        return;  // unknown peer: the frame would never have been addressed to it
    }
    peer->second.decode_faults.push_back(from);
}

void SimNetwork::advance(uint64_t now_ms) {
    now_ms_ = std::max(now_ms, now_ms_);
    while (!pending_.empty() && pending_.begin()->first.first <= now_ms_) {
        auto it = pending_.begin();
        const NodeId from = std::move(it->second.from);
        const NodeId to = std::move(it->second.to);
        const uint64_t seq = it->second.seq;
        Envelope envelope{.from = from, .message = Message{}};
        envelope.message = std::move(it->second.message);
        pending_.erase(it);

        peers_[to].inbox.push_back(std::move(envelope));
        ++delivered_;

        auto& state = link(from, to);
        if (seq < state.last_delivered) {
            ++reordered_;
        } else {
            state.last_delivered = seq;
        }
    }
}

void SimNetwork::add_peer(const NodeId& id) {
    peers_[id];
}

SimNetwork::LinkState& SimNetwork::link(const NodeId& from, const NodeId& to) {
    const auto key = make_pair(from, to);
    auto it = links_.find(key);
    if (it == links_.end()) {
        it = links_
                 .emplace(key, LinkState{.config = defaults_,
                                         .rng = mt19937_64(link_seed(seed_, from, to))})
                 .first;
    }
    return it->second;
}

void SimNetwork::schedule(const NodeId& from, const NodeId& to, const Message& message) {
    if (from == to) {
        return;  // the contract forbids self-delivery, so publishing to everyone you follow is safe
    }
    if (!peers_.contains(to)) {
        return;  // unknown peer: silent drop, per the transport contract
    }
    if (partitioned(from, to)) {
        ++dropped_;
        return;
    }

    auto& state = link(from, to);
    uniform_real_distribution<double> coin(0.0, 1.0);
    if (coin(state.rng) < state.config.loss) {
        ++dropped_;
        return;
    }

    uint32_t delay = state.config.delay_min_ms;
    if (state.config.delay_max_ms > state.config.delay_min_ms) {
        uniform_int_distribution<uint32_t> jitter(state.config.delay_min_ms,
                                                  state.config.delay_max_ms);
        delay = jitter(state.rng);
    }

    const auto enqueue = [&](uint64_t seq) {
        Pending entry{.from = from, .to = to, .seq = seq, .message = Message{}};
        entry.message = message;
        pending_.emplace(make_pair(now_ms_ + delay, seq), std::move(entry));
    };
    enqueue(++state.sent);
    if (coin(state.rng) < state.config.duplicate) {
        ++duplicated_;
        enqueue(++state.sent);
    }
}

SimTransport::SimTransport(SimNetwork& net, NodeId id) : net_(net), id_(std::move(id)) {
    net_.add_peer(id_);
}

void SimTransport::subscribe(const string& pattern) {
    net_.peers_[id_].subs.add(pattern);
}

void SimTransport::publish(const Channel& channel, Message message) {
    for (auto& [peer_id, peer] : net_.peers_) {
        if (peer_id != id_ && peer.subs.matches_any(channel)) {
            net_.schedule(id_, peer_id, message);
        }
    }
}

void SimTransport::send(const NodeId& to, Message message) {
    net_.schedule(id_, to, message);
}

vector<NodeId> SimTransport::peers() const {
    vector<NodeId> out;
    for (const auto& [peer_id, peer] : net_.peers_) {
        if (peer_id != id_) {
            out.push_back(peer_id);
        }
    }
    return out;
}

vector<NodeId> SimTransport::take_decode_faults() {
    vector<NodeId> out;
    out.swap(net_.peers_[id_].decode_faults);
    return out;
}

optional<Envelope> SimTransport::poll() {
    auto& inbox = net_.peers_[id_].inbox;
    if (inbox.empty()) {
        return nullopt;
    }
    Envelope envelope = std::move(inbox.front());
    inbox.pop_front();
    return envelope;
}

}  // namespace mosaic
