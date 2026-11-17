#include <cstdio>
#include <doctest/doctest.h>
#include <mosaic/net/peer_pins.h>

using namespace mosaic;

TEST_CASE("open mode pins on first use and refuses a changed key")
{
    PeerPins pins(PeerPins::Mode::Open);

    CHECK(pins.observe("b", "key-one") == PinVerdict::AllowedAndPinned);
    CHECK(pins.size() == 1);
    CHECK(pins.observe("b", "key-one") == PinVerdict::Allowed);
    // Same peer, different key: this is what impersonation looks like on the wire.
    CHECK(pins.observe("b", "key-two") == PinVerdict::Mismatch);
    // And the pin is NOT overwritten by the attempt.
    CHECK(pins.pinned("b") == std::optional<std::string>("key-one"));
}

TEST_CASE("closed mode refuses anything not pinned up front")
{
    PeerPins pins(PeerPins::Mode::Closed);
    pins.pin("b", "key-one");

    CHECK(pins.observe("b", "key-one") == PinVerdict::Allowed);
    CHECK(pins.observe("b", "key-two") == PinVerdict::Mismatch);
    CHECK(pins.observe("stranger", "key-three") == PinVerdict::NotListed);
    CHECK(pins.size() == 1); // a refused peer is not quietly added
}

TEST_CASE("pins survive a save and reload")
{
    const std::string path = "test-pins.txt";
    std::remove(path.c_str());

    PeerPins pins(PeerPins::Mode::Open);
    pins.observe("b", "key-one");
    pins.observe("c", "key-two");
    REQUIRE(pins.save(path));

    auto reloaded = PeerPins::load(path, PeerPins::Mode::Open);
    REQUIRE(reloaded.has_value());
    CHECK(reloaded->size() == 2);
    CHECK(reloaded->pinned("b") == std::optional<std::string>("key-one"));
    CHECK(reloaded->observe("c", "key-two") == PinVerdict::Allowed);
    std::remove(path.c_str());

    // A file we do not understand must fail closed, not accept everyone.
    {
        std::FILE* file = std::fopen(path.c_str(), "w");
        REQUIRE(file != nullptr);
        std::fputs("not-our-format\nb key-one\n", file);
        std::fclose(file);
    }
    CHECK_FALSE(PeerPins::load(path, PeerPins::Mode::Open).has_value());
    std::remove(path.c_str());

    // A missing file is a first run, not an error.
    const auto fresh = PeerPins::load("no-such-pins-file.txt", PeerPins::Mode::Open);
    REQUIRE(fresh.has_value());
    CHECK(fresh->size() == 0);
}

TEST_CASE("the handshake decision answers for both policies")
{
    // Open: first sight of an address is pinned; a later change from that address is refused.
    PeerPins open(PeerPins::Mode::Open);
    CHECK(open.decide("10.0.0.7", "key-one") == PinVerdict::AllowedAndPinned);
    CHECK(open.decide("10.0.0.7", "key-one") == PinVerdict::Allowed);
    CHECK(open.decide("10.0.0.7", "key-two") == PinVerdict::Mismatch);
    CHECK(open.decide("10.0.0.8", "key-three") == PinVerdict::AllowedAndPinned); // a different address

    // Closed: the key itself must be listed, because ZeroMQ's handshake never exposes a peer identity.
    PeerPins closed(PeerPins::Mode::Closed);
    closed.pin("b", "key-one");
    CHECK(closed.allows_key("key-one"));
    CHECK_FALSE(closed.allows_key("key-two"));
    CHECK(closed.decide("10.0.0.7", "key-one") == PinVerdict::Allowed);
    CHECK(closed.decide("10.0.0.7", "key-two") == PinVerdict::NotListed);
}
