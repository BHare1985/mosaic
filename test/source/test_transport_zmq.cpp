// The transport contract, run against the real ZeroMQ transport over loopback TCP (ZMQ-1). The suite
// is reused unchanged: adding a transport costs exactly this factory and nothing else.
#include "transport_conformance.h"

#include <chrono>
#include <doctest/doctest.h>
#include <memory>
#include <mosaic/net/zmq_transport.h>
#include <thread>

using namespace mosaic;
using mosaic::test::TransportPair;

namespace {
/// ZeroMQ delivers in real time, so the suite's settle() has to give the sockets a moment. This is the
/// only place in the suite where wall-clock time is involved, and it is bounded.
void settle()
{
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
}
} // namespace

TEST_CASE("transport conformance: ZeroMQ over loopback")
{
    const auto factory = [] {
        ZmqConfig a_cfg;
        a_cfg.id = "a";
        ZmqConfig b_cfg;
        b_cfg.id = "b";
        auto a = std::make_shared<ZmqTransport>(a_cfg);
        auto b = std::make_shared<ZmqTransport>(b_cfg);

        // Endpoints are only known after binding, so peers are wired once both ends exist.
        a->add_peer(ZmqPeerEndpoint { "b", b->pub_endpoint(), b->router_endpoint() });
        b->add_peer(ZmqPeerEndpoint { "a", a->pub_endpoint(), a->router_endpoint() });

        // The pair's unique_ptrs are views; the transports themselves are owned by the pair's `owner`,
        // which is destroyed last.
        struct Holder {
            std::shared_ptr<ZmqTransport> a;
            std::shared_ptr<ZmqTransport> b;
        };
        mosaic::test::TransportPair pair;
        pair.owner = std::make_shared<Holder>(Holder { a, b });
        pair.a = a.get();
        pair.b = b.get();
        pair.settle = settle;
        return pair;
    };
    mosaic::test::run_transport_conformance(factory);
}

namespace {
/// Drains everything currently available, retrying a bounded number of times while the link settles.
std::vector<Envelope> collect(Transport& transport, TransportPair& pair, int attempts = 10)
{
    std::vector<Envelope> out;
    for (int i = 0; i < attempts; ++i) {
        while (auto envelope = transport.poll())
            out.push_back(std::move(*envelope));
        if (!out.empty())
            break;
        pair.settle();
    }
    return out;
}
} // namespace

// The byte-prefix subscription cannot express "exactly one segment", so the transport must re-check the
// real pattern. This is the case that proves the coarse filter is not trusted (ZMQ-1, 03-matching).
TEST_CASE("ZeroMQ prefix subscription does not admit a deeper channel")
{
    ZmqConfig a_cfg;
    a_cfg.id = "a";
    ZmqConfig b_cfg;
    b_cfg.id = "b";
    ZmqTransport a(a_cfg);
    ZmqTransport b(b_cfg);
    a.add_peer(ZmqPeerEndpoint { "b", b.pub_endpoint(), b.router_endpoint() });
    b.add_peer(ZmqPeerEndpoint { "a", a.pub_endpoint(), a.router_endpoint() });

    b.subscribe("storage.*.shard7");
    b.subscribe("events.>");

    TransportPair pair;
    pair.settle = settle;

    const auto head_for = [](const char* channel, std::uint64_t seq) {
        return Message { HeadMsg { LogKey { "writer", channel }, seq, Hash { } } };
    };

    // Matches the star pattern: exactly one segment in the wildcard's place. Repeat the publish until
    // the subscription has propagated - best effort means the first one may be lost, not that none arrive.
    std::vector<Envelope> matched;
    for (int attempt = 0; attempt < 30 && matched.empty(); ++attempt) {
        a.publish("storage.eu.shard7", head_for("storage.eu.shard7", 1));
        pair.settle();
        matched = collect(b, pair, 1);
    }
    REQUIRE_FALSE(matched.empty());
    CHECK(matched.front().message.index() == 2); // HeadMsg

    // Same byte prefix ("storage."), two segments where the pattern allows one: must be refused.
    for (int i = 0; i < 3; ++i) {
        a.publish("storage.eu.frankfurt.shard7", head_for("storage.eu.frankfurt.shard7", 2));
        pair.settle();
    }
    auto deeper = 0;
    while (auto envelope = b.poll()) {
        if (const auto* head = std::get_if<HeadMsg>(&envelope->message))
            if (head->key.channel == "storage.eu.frankfurt.shard7")
                ++deeper;
    }
    CHECK(deeper == 0);
}

// Queue limits are explicit, and overflow is a counted drop rather than a blocked caller (FLOW/HWM).
TEST_CASE("ZeroMQ unicast overflow is counted, never blocking")
{
    ZmqConfig cfg;
    cfg.id = "sender";
    cfg.send_hwm = 1; // deliberately tiny, so overflow is certain
    cfg.receive_hwm = 1;
    cfg.pub_bind = "tcp://127.0.0.1:*";
    cfg.router_bind = "tcp://127.0.0.1:*";
    // A peer that never reads: nothing is listening on these endpoints.
    cfg.peers.push_back(ZmqPeerEndpoint { "gone", "tcp://127.0.0.1:1", "tcp://127.0.0.1:1" });

    ZmqTransport transport(cfg);
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 200; ++i)
        transport.send("gone", HeadMsg { LogKey { "w", "c" }, static_cast<std::uint64_t>(i), Hash { } });
    const auto elapsed = std::chrono::steady_clock::now() - start;

    CHECK(transport.dropped() > 0); // overflow is visible, not silent
    CHECK(elapsed < std::chrono::seconds(2)); // and it never blocked the caller
}
