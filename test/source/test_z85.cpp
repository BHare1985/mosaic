#include <doctest/doctest.h>
#include <mosaic/core/keystore.h>
#include <mosaic/core/z85.h>

using namespace mosaic;

// ZeroMQ RFC 32's own test vector: the encoding has to match libzmq byte for byte, or CURVE keys
// would be rejected by every other ZeroMQ implementation.
TEST_CASE("z85 matches the reference vector")
{
    const Bytes bytes { 0x86, 0x4f, 0xd2, 0x6f, 0xb5, 0x59, 0xf7, 0x5b };
    const auto encoded = z85_encode(bytes.data(), bytes.size());
    CHECK(encoded == "HelloWorld");

    const auto decoded = z85_decode(encoded);
    REQUIRE(decoded.has_value());
    CHECK(Bytes(decoded->begin(), decoded->end()) == bytes);
}

TEST_CASE("z85 round-trips and rejects malformed input")
{
    Bytes bytes(32);
    for (std::size_t i = 0; i < bytes.size(); ++i)
        bytes[i] = static_cast<std::uint8_t>((i * 37 + 11) & 0xff);

    const auto encoded = z85_encode(bytes.data(), bytes.size());
    CHECK(encoded.size() == 40); // 32 bytes -> 40 characters, the length ZeroMQ expects
    const auto decoded = z85_decode(encoded);
    REQUIRE(decoded.has_value());
    CHECK(Bytes(decoded->begin(), decoded->end()) == bytes);

    CHECK(z85_encode(bytes.data(), 30).empty()); // not a multiple of 4
    CHECK_FALSE(z85_decode("abcd").has_value()); // not a multiple of 5
    CHECK_FALSE(z85_decode("abc~d").has_value()); // '~' is not in the alphabet
    CHECK_FALSE(z85_decode("~~~~~").has_value());
}

#ifdef MOSAIC_HAS_SODIUM

TEST_CASE("the CURVE identity derives from the signing identity and is stable")
{
    const auto keystore = generate_keystore();
    REQUIRE(keystore.has_value());

    const auto curve = curve_identity_from_keystore(*keystore);
    REQUIRE(curve.has_value());
    CHECK(curve->public_z85.size() == 40); // z85 of 32 bytes
    CHECK(curve->secret_z85.size() == 40);
    CHECK(curve->public_z85 != curve->secret_z85);

    // Same keystore, same CURVE keys: one identity, two representations.
    const auto again = curve_identity_from_keystore(*keystore);
    REQUIRE(again.has_value());
    CHECK(again->public_z85 == curve->public_z85);
    CHECK(again->secret_z85 == curve->secret_z85);

    // A different identity cannot produce the same link key.
    const auto other = generate_keystore();
    REQUIRE(other.has_value());
    const auto other_curve = curve_identity_from_keystore(*other);
    REQUIRE(other_curve.has_value());
    CHECK(other_curve->public_z85 != curve->public_z85);

    // And it survives a save/load round trip, so a restart keeps the link identity too.
    const std::string path = "test-keystore-curve.key";
    std::remove(path.c_str());
    REQUIRE(save_keystore(*keystore, path));
    const auto reloaded = load_keystore(path);
    REQUIRE(reloaded.has_value());
    const auto reloaded_curve = curve_identity_from_keystore(*reloaded);
    REQUIRE(reloaded_curve.has_value());
    CHECK(reloaded_curve->public_z85 == curve->public_z85);
    std::remove(path.c_str());
}

#endif // MOSAIC_HAS_SODIUM
