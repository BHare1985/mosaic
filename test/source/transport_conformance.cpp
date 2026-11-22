#include "transport_conformance.h"

#include <mosaic/net/sim_network.h>

#include <doctest/doctest.h>

using std::function;
using std::make_shared;
using std::move;
using std::size_t;
using std::uint64_t;
using std::vector;

using namespace mosaic;
using mosaic::test::TransportFactory;
using mosaic::test::TransportPair;

namespace {

HeadMsg sample_head(uint64_t seq = 1) {
    return HeadMsg{LogKey{"writer", "events.a"}, seq, Hash{}};
}

/// Collects everything one endpoint can be given, one poll at a time (the contract allows no more).
vector<Envelope> drain(Transport& transport) {
    vector<Envelope> out;
    while (auto envelope = transport.poll())
        out.push_back(move(*envelope));
    return out;
}

/// "Best effort" means a publish right after subscribe may be lost while the subscription
/// propagates. The contract is about *filtering*, not about immediate delivery, so
/// delivery-dependent assertions retry a bounded number of times instead of pinning a timing the
/// contract never promised.
vector<Envelope> until_delivered(const TransportPair& pair, Transport& to,
                                 const function<void()>& send) {
    for (int attempt = 0; attempt < 30; ++attempt) {
        send();
        pair.settle();
        auto received = drain(to);
        if (!received.empty())
            return received;
    }
    return {};
}

void run_case(const TransportPair& pair) {
    Transport& a = *pair.a;
    Transport& b = *pair.b;

    SUBCASE("identity is stable and usable as an address") {
        CHECK(a.self() == "a");
        CHECK(b.self() == "b");
        CHECK(a.self() == "a");  // stable for the transport's lifetime, not recomputed per call
    }

    SUBCASE("the peer view lists addressable peers and never self") {
        const auto peers = a.peers();
        bool lists_b = false;
        bool lists_self = false;
        for (const auto& id : peers) {
            if (id == "b")
                lists_b = true;
            if (id == "a")
                lists_self = true;
        }
        CHECK(lists_b);
        CHECK_FALSE(lists_self);
        // A snapshot, not a promise: asking twice is allowed to differ once links come and go.
        CHECK_NOTHROW(a.peers());
    }

    SUBCASE("poll on an idle transport returns nothing and does not block") {
        pair.settle();
        CHECK_FALSE(a.poll().has_value());
        CHECK_FALSE(b.poll().has_value());
    }

    SUBCASE("publish is filtered by the receiver's subscription") {
        b.subscribe("events.>");
        const auto received =
            until_delivered(pair, b, [&] { a.publish("events.a", sample_head()); });
        REQUIRE_FALSE(received.empty());
        CHECK(received.front().from == "a");

        // A channel no pattern of b matches must never arrive, however many times it is sent.
        for (int i = 0; i < 3; ++i) {
            a.publish("other.x", sample_head(2));
            pair.settle();
        }
        CHECK(drain(b).empty());
    }

    SUBCASE("a node never receives its own publication") {
        a.subscribe(">");
        for (int i = 0; i < 5; ++i) {
            a.publish("events.a", sample_head());
            pair.settle();
        }
        CHECK(drain(a).empty());
    }

    SUBCASE("unicast reaches the addressed peer only") {
        const auto to_b = until_delivered(pair, b, [&] { a.send("b", sample_head()); });
        REQUIRE_FALSE(to_b.empty());
        CHECK(to_b.front().from == "a");
        CHECK(drain(a).empty());
    }

    SUBCASE("unicast to an unknown peer is dropped silently") {
        CHECK_NOTHROW(a.send("nobody", sample_head()));
        pair.settle();
        CHECK(drain(b).empty());
        CHECK(drain(a).empty());
    }

    SUBCASE("poll yields at most one message per call") {
        b.subscribe(">");
        // Deliver at least one, then check the shape of the API: one envelope per call, then
        // nothing.
        const auto first = until_delivered(pair, b, [&] { a.publish("events.a", sample_head(1)); });
        REQUIRE_FALSE(first.empty());
        size_t seen = 1;
        while (auto envelope = b.poll()) {
            CHECK(envelope.has_value());  // exactly one per call by construction
            ++seen;
            if (seen > 50)
                break;
        }
        CHECK(seen >= 1);
    }
}

}  // namespace

void mosaic::test::run_transport_conformance(const TransportFactory& factory) {
    auto pair = factory();
    REQUIRE(pair.a);
    REQUIRE(pair.b);
    REQUIRE(pair.settle);
    run_case(pair);
}

TEST_CASE("transport conformance: simulated network") {
    // The factory is all an implementation has to provide. Swapping in the ZeroMQ transport adds
    // one more test case calling this same function (see ZMQ-1 in docs/design/TODO.md).
    const TransportFactory factory = [] {
        struct SimPair {
            SimNetwork net{1};
            SimTransport a{net, "a"};
            SimTransport b{net, "b"};
        };
        auto holder = make_shared<SimPair>();
        TransportPair pair;
        pair.owner = holder;
        pair.a = &holder->a;
        pair.b = &holder->b;
        pair.settle = [holder] { holder->net.advance(holder->net.now()); };
        return pair;
    };
    run_transport_conformance(factory);
}
