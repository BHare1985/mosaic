// The transport contract, run against the real ZeroMQ transport over loopback TCP (ZMQ-1). The
// suite is reused unchanged: adding a transport costs exactly this factory and nothing else.
#include <mosaic/core/keystore.h>
#include <mosaic/net/peer_pins.h>
#include <mosaic/net/zmq_transport.h>

#include <chrono>
#include <doctest/doctest.h>
#include <memory>
#include <thread>

#include "transport_conformance.h"

namespace chrono = std::chrono;
namespace this_thread = std::this_thread;
using std::function;
using std::get_if;
using std::make_shared;
using std::move;
using std::remove;
using std::shared_ptr;
using std::string;
using std::uint64_t;
using std::vector;

using namespace mosaic;
using mosaic::test::TransportPair;

namespace {
/// ZeroMQ delivers in real time, so the suite's settle() has to give the sockets a moment. This is
/// the only place in the suite where wall-clock time is involved, and it is bounded.
void settle() {
    this_thread::sleep_for(chrono::milliseconds(150));
}

#ifdef MOSAIC_HAS_SODIUM
/// With CURVE, libzmq blocks a handshake until the ZAP handler answers, so a caller that only
/// sleeps would stall every connection. This services the handlers while time passes.
function<void()> settle_with(vector<ZmqTransport*> transports) {
    return [transports] {
        for (int i = 0; i < 6; ++i) {
            for (auto* transport : transports)
                if (transport)
                    transport->service_handshakes();
            this_thread::sleep_for(chrono::milliseconds(25));
        }
    };
}
#endif  // MOSAIC_HAS_SODIUM
}  // namespace

TEST_CASE("transport conformance: ZeroMQ over loopback") {
    const auto factory = [] {
        ZmqConfig a_cfg;
        a_cfg.id = "a";
        ZmqConfig b_cfg;
        b_cfg.id = "b";
        auto a = make_shared<ZmqTransport>(a_cfg);
        auto b = make_shared<ZmqTransport>(b_cfg);

        // Endpoints are only known after binding, so peers are wired once both ends exist.
        a->add_peer(ZmqPeerEndpoint{"b", b->pub_endpoint(), b->router_endpoint(), ""});
        b->add_peer(ZmqPeerEndpoint{"a", a->pub_endpoint(), a->router_endpoint(), ""});

        // The pair's unique_ptrs are views; the transports themselves are owned by the pair's
        // `owner`, which is destroyed last.
        struct Holder {
            shared_ptr<ZmqTransport> a;
            shared_ptr<ZmqTransport> b;
        };
        mosaic::test::TransportPair pair;
        pair.owner = make_shared<Holder>(Holder{a, b});
        pair.a = a.get();
        pair.b = b.get();
        pair.settle = settle;
        return pair;
    };
    mosaic::test::run_transport_conformance(factory);
}

namespace {
/// Drains everything currently available, retrying a bounded number of times while the link
/// settles.
vector<Envelope> collect(Transport& transport, TransportPair& pair, int attempts = 10) {
    vector<Envelope> out;
    for (int i = 0; i < attempts; ++i) {
        while (auto envelope = transport.poll())
            out.push_back(move(*envelope));
        if (!out.empty())
            break;
        pair.settle();
    }
    return out;
}
}  // namespace

// The byte-prefix subscription cannot express "exactly one segment", so the transport must re-check
// the real pattern. This is the case that proves the coarse filter is not trusted (ZMQ-1,
// 03-matching).
TEST_CASE("ZeroMQ prefix subscription does not admit a deeper channel") {
    ZmqConfig a_cfg;
    a_cfg.id = "a";
    ZmqConfig b_cfg;
    b_cfg.id = "b";
    ZmqTransport a(a_cfg);
    ZmqTransport b(b_cfg);
    a.add_peer(ZmqPeerEndpoint{"b", b.pub_endpoint(), b.router_endpoint(), ""});
    b.add_peer(ZmqPeerEndpoint{"a", a.pub_endpoint(), a.router_endpoint(), ""});

    b.subscribe("storage.*.shard7");
    b.subscribe("events.>");

    TransportPair pair;
    pair.settle = settle;

    const auto head_for = [](const char* channel, uint64_t seq) {
        return Message{HeadMsg{LogKey{"writer", channel}, seq, Hash{}}};
    };

    // Matches the star pattern: exactly one segment in the wildcard's place. Repeat the publish
    // until the subscription has propagated - best effort means the first one may be lost, not that
    // none arrive.
    vector<Envelope> matched;
    for (int attempt = 0; attempt < 30 && matched.empty(); ++attempt) {
        a.publish("storage.eu.shard7", head_for("storage.eu.shard7", 1));
        pair.settle();
        matched = collect(b, pair, 1);
    }
    REQUIRE_FALSE(matched.empty());
    CHECK(matched.front().message.index() == 2);  // HeadMsg

    // Same byte prefix ("storage."), two segments where the pattern allows one: must be refused.
    for (int i = 0; i < 3; ++i) {
        a.publish("storage.eu.frankfurt.shard7", head_for("storage.eu.frankfurt.shard7", 2));
        pair.settle();
    }
    auto deeper = 0;
    while (auto envelope = b.poll()) {
        if (const auto* head = get_if<HeadMsg>(&envelope->message))
            if (head->key.channel == "storage.eu.frankfurt.shard7")
                ++deeper;
    }
    CHECK(deeper == 0);
}

// Queue limits are explicit, and overflow is a counted drop rather than a blocked caller
// (FLOW/HWM).
TEST_CASE("ZeroMQ unicast overflow is counted, never blocking") {
    ZmqConfig cfg;
    cfg.id = "sender";
    cfg.send_hwm = 1;  // deliberately tiny, so overflow is certain
    cfg.receive_hwm = 1;
    cfg.pub_bind = "tcp://127.0.0.1:*";
    cfg.router_bind = "tcp://127.0.0.1:*";
    // A peer that never reads: nothing is listening on these endpoints.
    cfg.peers.push_back(ZmqPeerEndpoint{"gone", "tcp://127.0.0.1:1", "tcp://127.0.0.1:1", ""});

    ZmqTransport transport(cfg);
    const auto start = chrono::steady_clock::now();
    for (int i = 0; i < 200; ++i)
        transport.send("gone", HeadMsg{LogKey{"w", "c"}, static_cast<uint64_t>(i), Hash{}});
    const auto elapsed = chrono::steady_clock::now() - start;

    CHECK(transport.dropped() > 0);       // overflow is visible, not silent
    CHECK(elapsed < chrono::seconds(2));  // and it never blocked the caller
}

#ifdef MOSAIC_HAS_SODIUM

namespace {
/// A node's CURVE material, derived from its Ed25519 identity the way the product does it.
struct CurveNode {
    Keystore keystore;
    CurveIdentity curve;
};

CurveNode make_node() {
    CurveNode node{*generate_keystore(), {}};
    node.curve = *curve_identity_from_keystore(node.keystore);
    return node;
}
}  // namespace

// With CURVE on, the whole contract must still hold: encryption changes the options, not the
// semantics.
TEST_CASE("transport conformance: ZeroMQ with CURVE") {
    REQUIRE(ZmqTransport::curve_available());  // a libzmq without CURVE must fail here, not pass
                                               // quietly
    const auto factory = [] {
        const auto a_node = make_node();
        const auto b_node = make_node();

        ZmqConfig a_cfg;
        a_cfg.id = "a";
        a_cfg.curve = ZmqCurveConfig{true, a_node.curve.public_z85, a_node.curve.secret_z85, ""};
        ZmqConfig b_cfg;
        b_cfg.id = "b";
        b_cfg.curve = ZmqCurveConfig{true, b_node.curve.public_z85, b_node.curve.secret_z85, ""};

        auto a = make_shared<ZmqTransport>(a_cfg);
        auto b = make_shared<ZmqTransport>(b_cfg);
        a->add_peer(
            ZmqPeerEndpoint{"b", b->pub_endpoint(), b->router_endpoint(), b_node.curve.public_z85});
        b->add_peer(
            ZmqPeerEndpoint{"a", a->pub_endpoint(), a->router_endpoint(), a_node.curve.public_z85});

        struct Holder {
            shared_ptr<ZmqTransport> a;
            shared_ptr<ZmqTransport> b;
        };
        TransportPair pair;
        pair.owner = make_shared<Holder>(Holder{a, b});
        pair.a = a.get();
        pair.b = b.get();
        pair.settle = settle_with({a.get(), b.get()});
        return pair;
    };
    mosaic::test::run_transport_conformance(factory);
}

// CURVE authenticates the server to the client; the handshake decision authenticates the client to
// the server. Together a peer cannot claim an identity it does not hold (SEC-1b).
TEST_CASE("a peer presenting an unlisted key is refused at handshake") {
    REQUIRE(ZmqTransport::curve_available());
    const auto a_node = make_node();
    const auto b_node = make_node();
    const auto imposter_node = make_node();

    const string pins_path = "test-zmq-pins.txt";
    remove(pins_path.c_str());
    {
        PeerPins pins(PeerPins::Mode::Closed);
        pins.pin("b", b_node.curve.public_z85);  // only b's key may connect
        REQUIRE(pins.save(pins_path));
    }

    ZmqConfig a_cfg;
    a_cfg.id = "a";
    a_cfg.link_policy = ZmqLinkPolicy::Closed;
    a_cfg.curve = ZmqCurveConfig{true, a_node.curve.public_z85, a_node.curve.secret_z85, pins_path};
    ZmqTransport a(a_cfg);

    // The imposter speaks CURVE correctly and addresses a properly, but is not the listed key.
    ZmqConfig imposter_cfg;
    imposter_cfg.id = "b";
    imposter_cfg.curve =
        ZmqCurveConfig{true, imposter_node.curve.public_z85, imposter_node.curve.secret_z85, ""};
    ZmqTransport imposter(imposter_cfg);
    imposter.add_peer(
        ZmqPeerEndpoint{"a", a.pub_endpoint(), a.router_endpoint(), a_node.curve.public_z85});

    const auto settle_imposter = settle_with({&a, &imposter});
    for (int attempt = 0; attempt < 6; ++attempt) {
        imposter.send("a", HeadMsg{LogKey{"w", "c"}, static_cast<uint64_t>(attempt), Hash{}});
        settle_imposter();
        CHECK_FALSE(a.poll().has_value());  // refused at the handshake
    }
    CHECK(a.curve_rejects() > 0);

    // The genuine holder of the listed key is admitted.
    ZmqConfig b_cfg;
    b_cfg.id = "b";
    b_cfg.curve = ZmqCurveConfig{true, b_node.curve.public_z85, b_node.curve.secret_z85, ""};
    ZmqTransport b(b_cfg);
    b.add_peer(
        ZmqPeerEndpoint{"a", a.pub_endpoint(), a.router_endpoint(), a_node.curve.public_z85});

    const auto settle_genuine = settle_with({&a, &b});
    bool delivered = false;
    for (int attempt = 0; attempt < 30 && !delivered; ++attempt) {
        b.send("a", HeadMsg{LogKey{"w", "c"}, 42, Hash{}});
        settle_genuine();
        while (auto envelope = a.poll())
            delivered = true;
    }
    CHECK(delivered);
    remove(pins_path.c_str());
}

TEST_CASE("CURVE refuses a client configured with the wrong server key") {
    REQUIRE(ZmqTransport::curve_available());
    const auto a_node = make_node();
    const auto b_node = make_node();
    const auto stranger = make_node();

    ZmqConfig a_cfg;
    a_cfg.id = "a";
    a_cfg.curve = ZmqCurveConfig{true, a_node.curve.public_z85, a_node.curve.secret_z85, ""};
    ZmqTransport a(a_cfg);

    ZmqConfig b_cfg;
    b_cfg.id = "b";
    b_cfg.curve = ZmqCurveConfig{true, b_node.curve.public_z85, b_node.curve.secret_z85, ""};
    ZmqTransport b(b_cfg);
    b.add_peer(
        ZmqPeerEndpoint{"a", a.pub_endpoint(), a.router_endpoint(), stranger.curve.public_z85});

    const auto settle_all = settle_with({&a, &b});
    for (int attempt = 0; attempt < 10; ++attempt) {
        b.send("a", HeadMsg{LogKey{"w", "c"}, 1, Hash{}});
        settle_all();
        CHECK_FALSE(a.poll().has_value());
    }
}

#endif  // MOSAIC_HAS_SODIUM
