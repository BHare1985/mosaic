#pragma once

#include <mosaic/core/types.h>
#include <utility>
#include <vector>

namespace mosaic {

/// Where do I find peers? Deliberately tiny: the Node does not use it yet (DISC-*). Implementations
/// to write: static list (below), gossip over the log protocol, LAN beacon, rendezvous server.
class Discovery {
public:
    virtual ~Discovery() = default;
    virtual std::vector<NodeId> peers() const = 0;
};

class StaticDiscovery final : public Discovery {
public:
    explicit StaticDiscovery(std::vector<NodeId> peers)
        : peers_(std::move(peers))
    {
    }
    std::vector<NodeId> peers() const override { return peers_; }

private:
    std::vector<NodeId> peers_;
};

} // namespace mosaic
