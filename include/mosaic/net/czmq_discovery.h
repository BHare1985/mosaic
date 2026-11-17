#pragma once

// LAN discovery and peer gossip over czmq's actors (spec: discovery/peer-discovery).
//
// Shape borrowed from zyre, which solved this problem the same way: a UDP beacon answers "who is on my
// network". Gossip - zyre's second hop, for peers on another subnet - is *not* wired here: czmq's zgossip
// actor accepts commands publicly but emits its tuples as private zproto frames (zgossip_msg.h lives in
// czmq's src/, not its include/), so an outside consumer cannot correctly parse what it sends back. Doing it
// ourselves over our own protocol is DISC-3, still open. Dafka's alternative - a tower - is a central
// rendezvous this project deliberately does not have.
//
// Threading: each actor owns a thread. Nothing is shared with the node: the daemon calls
// `poll_discovered()` once per loop iteration and receives candidates as values, so the node stays
// single-threaded and there is no lock anywhere.

#include <cstdint>
#include <mosaic/net/peer_discovery.h>
#include <string>

typedef struct _zactor_t zactor_t; // opaque: no czmq header in this one

namespace mosaic {

struct CzmqDiscoveryConfig {
    std::string cluster = "mosaic"; // beacon filter and gossip namespace: scopes one deployment
    std::uint16_t beacon_port = 0; // UDP port to broadcast on; 0 disables the beacon
    std::uint32_t beacon_interval_ms = 1000;
    std::string interface; // optional zsys interface, e.g. "127.0.0.1" for tests
    DiscoveredPeer self; // what this node announces (id, endpoint, curve key)
};

class CzmqDiscovery final : public PeerDiscovery {
public:
    explicit CzmqDiscovery(CzmqDiscoveryConfig config);
    ~CzmqDiscovery() override;

    CzmqDiscovery(const CzmqDiscovery&) = delete;
    CzmqDiscovery& operator=(const CzmqDiscovery&) = delete;

    std::vector<DiscoveredPeer> poll_discovered() override;
    const char* kind() const override;

    /// True when the beacon actually configured. A host without a broadcast-capable interface returns
    /// false, which is worth reporting rather than looking like "nobody is out there".
    bool beacon_active() const;

private:
    struct Impl;
    Impl& impl_;
};

} // namespace mosaic
