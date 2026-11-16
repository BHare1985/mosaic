#include <doctest/doctest.h>
#include <mosaic/core/keystore.h>
#include <mosaic/core/record.h>
#include <mosaic/proto/codec.h>

#include <cstdio>
#include <fstream>
#include <string>

#ifndef _WIN32
#include <sys/stat.h>
#endif

using namespace mosaic;

TEST_CASE("hash algorithms are tagged and dispatched by the record")
{
    const Bytes data { 'a', 'b', 'c' };
    const auto sha = hash_with(HashAlgo::Sha256, data);
    REQUIRE(sha.has_value());
    CHECK(to_hex(*sha) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    CHECK(hash_algo_supported(HashAlgo::Sha256));

#ifdef MOSAIC_HAS_SODIUM
    CHECK(hash_algo_supported(HashAlgo::Blake2b));
    const auto blake = hash_with(HashAlgo::Blake2b, data);
    REQUIRE(blake.has_value());
    CHECK(to_hex(*blake) != to_hex(*sha)); // different algorithm, different digest
    CHECK(kDefaultHashAlgo == HashAlgo::Blake2b);
#else
    // A dependency-free build cannot compute BLAKE2b, and must say so rather than return zeros.
    CHECK_FALSE(hash_algo_supported(HashAlgo::Blake2b));
    CHECK_FALSE(hash_with(HashAlgo::Blake2b, data).has_value());
    CHECK(kDefaultHashAlgo == HashAlgo::Sha256);
#endif
}

#ifdef MOSAIC_HAS_SODIUM

TEST_CASE("Ed25519 signs and verifies, and tampering fails")
{
    const auto keypair = ed25519_generate();
    REQUIRE(keypair.has_value());
    Ed25519Signer signer(keypair->first, keypair->second);
    Ed25519Verifier verifier;

    Sha256Hasher hasher;
    Record record;
    record.writer = signer.id();
    record.channel = "events.a";
    record.hash_algo = HashAlgo::Sha256;
    record.payload = Bytes { 'h', 'i' };
    record.sig = signer.sign(record_hash(record, hasher));

    CHECK(verifier.verify(record.writer, record_hash(record, hasher), record.sig));

    Record tampered = record;
    tampered.payload = Bytes { 'n', 'o' };
    CHECK_FALSE(verifier.verify(tampered.writer, record_hash(tampered, hasher), tampered.sig));

    Record short_sig = record;
    short_sig.sig.resize(10);
    CHECK_FALSE(verifier.verify(short_sig.writer, record_hash(short_sig, hasher), short_sig.sig));
}

TEST_CASE("another key cannot claim an identity")
{
    const auto first = ed25519_generate();
    const auto second = ed25519_generate();
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    Ed25519Signer signer_a(first->first, first->second);
    Ed25519Signer signer_b(second->first, second->second);
    Ed25519Verifier verifier;

    const Hash hash = sha256(Bytes { 'x' });
    const Bytes signature = signer_a.sign(hash);
    CHECK(verifier.verify(signer_a.id(), hash, signature));
    // Signed by one key, presented under the other's identity: rejected.
    CHECK_FALSE(verifier.verify(signer_b.id(), hash, signature));
    // A non-hex identity is not a key we can verify, so it fails closed.
    CHECK_FALSE(verifier.verify("alice", hash, signature));
}

TEST_CASE("the writer identity is the hex public key and survives a reload")
{
    const auto keystore = generate_keystore();
    REQUIRE(keystore.has_value());
    CHECK(keystore->id().size() == 64); // 32-byte key, two hex chars each
    CHECK(keystore->id() == to_hex(keystore->public_key));

    const std::string path = "test-keystore-identity.key";
    std::remove(path.c_str());
    REQUIRE(save_keystore(*keystore, path));

    const auto reloaded = load_keystore(path);
    REQUIRE(reloaded.has_value());
    CHECK(reloaded->id() == keystore->id()); // same log ownership after a restart
    CHECK(reloaded->secret_key == keystore->secret_key);
    std::remove(path.c_str());
}

TEST_CASE("keystore files are owner-only and written atomically")
{
#ifndef _WIN32
    const auto keystore = generate_keystore();
    REQUIRE(keystore.has_value());
    const std::string path = "test-keystore-mode.key";
    std::remove(path.c_str());
    REQUIRE(save_keystore(*keystore, path));

    struct stat info { };
    REQUIRE(::stat(path.c_str(), &info) == 0);
    CHECK((info.st_mode & (S_IRWXG | S_IRWXO)) == 0); // nothing for group or others
    CHECK((info.st_mode & S_IRUSR) != 0);

    // The temporary file used for the rename is gone, and no partial key is left behind.
    CHECK(::stat((path + ".tmp").c_str(), &info) != 0);
    std::remove(path.c_str());
#endif
}

TEST_CASE("a mismatched keystore is rejected rather than half-loaded")
{
    const std::string path = "test-keystore-bad.key";
    {
        std::ofstream out(path);
        out << "mosaic-keystore 1\npublic zzzz\nsecret 00\n";
    }
    CHECK_FALSE(load_keystore(path).has_value());
    std::remove(path.c_str());
    CHECK_FALSE(load_keystore("does-not-exist.key").has_value());
}

TEST_CASE("a signed record survives encode, transmit and decode with verification intact")
{
    Sha256Hasher hasher;
    const auto keystore = generate_keystore();
    REQUIRE(keystore.has_value());
    Ed25519Signer signer(keystore->public_key, keystore->secret_key);
    Ed25519Verifier verifier;

    Record record;
    record.writer = signer.id();
    record.channel = "events.a";
    record.hash_algo = HashAlgo::Sha256;
    record.payload = Bytes { 'p', 'a', 'y' };
    record.sig = signer.sign(record_hash(record, hasher));

    const Bytes wire = encode_record(record);
    const auto received = decode_record(wire);
    REQUIRE(received.has_value());

    const auto hash = record_hash_tagged(*received);
    REQUIRE(hash.has_value());
    CHECK(*hash == record_hash(record, hasher));
    CHECK(verifier.verify(received->writer, *hash, received->sig));

    // One flipped byte in the canonical part must break verification, not just the hash.
    Bytes damaged = wire;
    damaged[10] = static_cast<std::uint8_t>(damaged[10] ^ 0x01);
    const auto damaged_record = decode_record(damaged);
    if (damaged_record) {
        const auto damaged_hash = record_hash_tagged(*damaged_record);
        CHECK((!damaged_hash || !verifier.verify(damaged_record->writer, *damaged_hash, damaged_record->sig)));
    }
}

#endif // MOSAIC_HAS_SODIUM
