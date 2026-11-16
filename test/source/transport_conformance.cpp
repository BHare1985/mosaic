#include "transport_conformance.h"

#include <doctest/doctest.h>
#include <mosaic/net/sim_network.h>

using namespace mosaic;
using mosaic::test::TransportFactory;
using mosaic::test::TransportPair;

namespace {

HeadMsg sample_head(std::uint64_t seq = 1)
{
    return HeadMsg { LogKey { "writer", "events.a" }, seq, Hash { } };
}

/// Collects everything one endpoint can be given, one poll at a time (the contract allows no more).
std::vector<Envelope> drain(Transport& transport)
{
    std::vector<Envelope> out;
    while (auto envelope = transport.poll())
        out.push_back(std::move(*envelope));
    return out;
}

void run_case(const TransportPair& pair)
{
    Transport& a = *pair.a;
    Transport& b = *pair.b;

    SUBCASE("identity is stable and usable as an address")
    {
        CHECK(a.self() == "a");
        CHECK(b.self() == "b");
        CHECK(a.self() == "a"); // stable for the transport's lifetime, not recomputed per call
    }

    SUBCASE("poll on an idle transport returns nothing and does not block")
    {
        pair.settle();
        CHECK_FALSE(a.poll().has_value());
        CHECK_FALSE(b.poll().has_value());
    }

    SUBCASE("publish is filtered by the receiver's subscription")
    {
        b.subscribe("events.>");
        a.publish("events.a", sample_head());
        pair.settle();

        const auto received = drain(b);
        REQUIRE(received.size() == 1);
        CHECK(received.front().from == "a");

        a.publish("other.x", sample_head(2)); // matches no pattern of b
        pair.settle();
        CHECK(drain(b).empty());
    }

    SUBCASE("a node never receives its own publication")
    {
        a.subscribe(">");
        a.publish("events.a", sample_head());
        pair.settle();
        CHECK(drain(a).empty());
    }

    SUBCASE("unicast reaches the addressed peer only")
    {
        a.send("b", sample_head());
        pair.settle();

        const auto to_b = drain(b);
        REQUIRE(to_b.size() == 1);
        CHECK(to_b.front().from == "a");
        CHECK(drain(a).empty());
    }

    SUBCASE("unicast to an unknown peer is dropped silently")
    {
        CHECK_NOTHROW(a.send("nobody", sample_head()));
        pair.settle();
        CHECK(drain(b).empty());
        CHECK(drain(a).empty());
    }

    SUBCASE("poll yields at most one message per call")
    {
        b.subscribe(">");
        a.publish("events.a", sample_head(1));
        a.publish("events.a", sample_head(2));
        pair.settle();

        CHECK(b.poll().has_value());
        CHECK(b.poll().has_value());
        CHECK_FALSE(b.poll().has_value());
    }
}

} // namespace

void mosaic::test::run_transport_conformance(const TransportFactory& factory)
{
    auto pair = factory();
    REQUIRE(pair.a);
    REQUIRE(pair.b);
    REQUIRE(pair.settle);
    run_case(pair);
}

TEST_CASE("transport conformance: simulated network")
{
    // The factory is all an implementation has to provide. Swapping in the ZeroMQ transport adds one
    // more test case calling this same function (see ZMQ-1 in docs/design/TODO.md).
    const TransportFactory factory = [] {
        auto net = std::make_shared<SimNetwork>(1);
        TransportPair pair;
        pair.owner = net; // declared first, so the network outlives both transports
        pair.a = std::make_unique<SimTransport>(*net, "a");
        pair.b = std::make_unique<SimTransport>(*net, "b");
        pair.settle = [net] { net->advance(net->now()); };
        return pair;
    };
    run_transport_conformance(factory);
}
