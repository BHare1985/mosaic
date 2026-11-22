// discovery/peer-discovery: candidates, bookkeeping, and the rule that discovery never grants
// trust.
#include <mosaic/core/keystore.h>
#include <mosaic/net/peer_discovery.h>
#include <mosaic/net/peer_pins.h>

#include <doctest/doctest.h>

using std::string;
using std::uint64_t;

using namespace mosaic;

namespace {
DiscoveredPeer peer(const string& id, const string& endpoint = "127.0.0.1:6100") {
    return DiscoveredPeer{id, endpoint, ""};
}
}  // namespace

TEST_CASE("the static list is a discovery source like any other") {
    StaticPeerDiscovery discovery({peer("a"), peer("b")});
    CHECK(string(discovery.kind()) == "static");

    const auto first = discovery.poll_discovered();
    REQUIRE(first.size() == 2);
    CHECK(first.front().id == "a");
    // Delivered once: the node dials them, it does not re-dial on every loop iteration.
    CHECK(discovery.poll_discovered().empty());
}

TEST_CASE("a peer is dialled once and refreshed, not re-dialled") {
    PeerBook book;
    CHECK(book.observe(peer("a"), 0));          // new: dial it
    CHECK_FALSE(book.observe(peer("a"), 100));  // seen again: no second dial
    CHECK(book.size() == 1);
    CHECK(book.knows("a"));
    CHECK(book.overflow() == 0);
}

TEST_CASE("a silent peer is forgotten after the expiry window") {
    PeerBook book(8, 300, 1000);
    CHECK(book.observe(peer("a"), 0));
    const auto peers = book.peers();
    CHECK_FALSE(peers.empty());

    CHECK(book.expire(500) == 0);  // still fresh
    CHECK(book.size() == 1);
    CHECK(book.expire(1000) == 1);  // expiry reached
    CHECK(book.size() == 0);

    // And if it announces again it is treated as new, so it is re-dialled.
    CHECK(book.observe(peer("a"), 2000));
}

TEST_CASE("the tracked set is bounded and the overflow is counted") {
    PeerBook book(2, 5000, 10000);
    CHECK(book.observe(peer("a"), 0));
    CHECK(book.observe(peer("b"), 0));
    CHECK_FALSE(book.observe(peer("c"), 0));  // book full
    CHECK(book.size() == 2);
    CHECK(book.overflow() == 1);
}

TEST_CASE("churn returns the set to the number of peers actually present") {
    PeerBook book(16, 50, 100);
    for (int round = 0; round < 5; ++round) {
        for (int i = 0; i < 6; ++i)
            book.observe(peer("peer-" + std::to_string(i)), static_cast<uint64_t>(round) * 100);
        book.expire(static_cast<uint64_t>(round) * 100);
    }
    CHECK(book.size() <= 6);
}

#ifdef MOSAIC_HAS_SODIUM
// The rule that matters: a discovered peer is a hint, and the key policy decides whether it is
// trusted.
TEST_CASE("a discovered peer is still subject to the key policy") {
    // Closed mode with one provisioned key: a peer discovered by beacon or gossip whose key is not
    // listed gets exactly the treatment a configured peer with that key would.
    PeerPins closed(PeerPins::Mode::Closed);
    closed.pin("known", "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");

    CHECK(closed.decide("discovered-address", "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA") ==
          PinVerdict::Allowed);
    CHECK(closed.decide("discovered-address", "ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ") ==
          PinVerdict::NotListed);

    // Open mode pins whatever it sees first, so discovery is usable without configuration - and a
    // later change of key from the same address is still refused.
    PeerPins open(PeerPins::Mode::Open);
    CHECK(open.decide("10.0.0.9", "key-one") == PinVerdict::AllowedAndPinned);
    CHECK(open.decide("10.0.0.9", "key-two") == PinVerdict::Mismatch);
}
#endif

TEST_CASE("a quiet peer goes evasive before it expires") {
    PeerBook book(8, 300, 1000);  // evasive at 300ms, gone at 1000ms
    REQUIRE(book.observe(peer("a"), 0));

    CHECK(book.take_evasive(200).empty());  // still talking
    const auto quiet = book.take_evasive(400);
    REQUIRE(quiet.size() == 1);
    CHECK(quiet.front() == "a");
    CHECK(book.size() == 1);                // evasive is not gone
    CHECK(book.take_evasive(500).empty());  // reported once, not every loop iteration

    // It announces again: no longer evasive, and it is not re-dialled.
    CHECK_FALSE(book.observe(peer("a"), 600));
    CHECK(book.take_evasive(700).empty());

    // Silence from 600 onward means expired at 1600.
    CHECK(book.expire(1200) == 0);
    CHECK(book.expire(1600) == 1);
    CHECK(book.size() == 0);
}
