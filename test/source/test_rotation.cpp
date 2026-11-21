// Key rotation: the link is an ordinary record, and following it is a local, bounded chain walk
// from a root the caller already trusts (spec: core/key-rotation, SEC-3).
#include <mosaic/core/insecure_test_signer.h>
#include <mosaic/core/key_rotation.h>
#include <mosaic/log/log.h>
#include <mosaic/log/rotation.h>
#include <mosaic/node/channel_rules.h>
#include <mosaic/store/memory_log_store.h>

#include <doctest/doctest.h>
#include <string>

using std::string;

using namespace mosaic;

namespace {
/// Writer ids are hex public keys, so a rotation successor has to look like one even in a test.
WriterId key_id(char hex_digit) {
    return string(64, hex_digit);
}

Bytes text(const string& s) {
    return Bytes(s.begin(), s.end());
}

/// Append a rotation link to `writer`'s log on `channel`, the way a writer would.
Record rotate(MemoryLogStore& store, const Signer& signer, const Hasher& hasher,
              const string& channel, const WriterId& successor) {
    RotationLink link;
    link.successor = successor;
    link.successor_start_seq = 0;
    link.hash_algo = hasher.algorithm();
    LogWriter writer(store, hasher, signer, channel);
    return writer.append(kRotationContentType, encode_rotation_link(link), 1000);
}

Record append_payload(MemoryLogStore& store, const Signer& signer, const Hasher& hasher,
                      const string& channel, const string& payload) {
    LogWriter writer(store, hasher, signer, channel);
    return writer.append("text/plain", text(payload), 1000);
}
}  // namespace

TEST_CASE("a rotation payload round-trips, and a damaged one decodes to nothing") {
    Sha256Hasher hasher;
    InsecureTestSigner alice(key_id('a'));
    RotationLink link;
    link.successor = alice.id();
    link.hash_algo = HashAlgo::Sha256;
    link.successor_start_seq = 7;

    const auto decoded = decode_rotation_link(encode_rotation_link(link));
    REQUIRE(decoded.has_value());
    CHECK(decoded->successor == link.successor);
    CHECK(decoded->successor_start_seq == 7);
    CHECK(decoded->hash_algo == HashAlgo::Sha256);

    // A truncated payload, an unknown format and an unusable key are all "nothing", never a
    // half-read successor.
    auto bytes = encode_rotation_link(link);
    bytes.resize(bytes.size() - 2);
    CHECK_FALSE(decode_rotation_link(bytes).has_value());

    auto future = encode_rotation_link(link);
    future[0] = 99;
    CHECK_FALSE(decode_rotation_link(future).has_value());

    RotationLink broken = link;
    broken.successor = "not-a-key";
    CHECK_FALSE(decode_rotation_link(encode_rotation_link(broken)).has_value());
    CHECK_FALSE(decode_rotation_link(Bytes{}).has_value());
    CHECK_FALSE(decode_rotation_link(Bytes(4096, 'x')).has_value());
}

TEST_CASE("a rotation link is an ordinary record: it replicates and it can be forged") {
    Sha256Hasher hasher;
    InsecureTestSigner alice(key_id('a')), mallory(key_id('d'));
    MemoryLogStore writer_store, replica_store;
    ReplicaLog replica(replica_store, hasher, alice);

    append_payload(writer_store, alice, hasher, "events.a", "before");
    const auto link = rotate(writer_store, alice, hasher, "events.a", mallory.id());
    append_payload(writer_store, alice, hasher, "events.a", "after");

    // A follower that knows nothing about rotation stores the link like any other record.
    for (Seq seq = 0; seq < 3; ++seq) {
        const auto record = writer_store.get({key_id('a'), "events.a"}, seq);
        REQUIRE(record.has_value());
        REQUIRE(replica.ingest(*record) == IngestResult::Accepted);
    }
    const auto stored = replica_store.get({key_id('a'), "events.a"}, 1);
    REQUIRE(stored.has_value());
    CHECK(stored->content_type == kRotationContentType);
    CHECK(replica_store.head_seq({key_id('a'), "events.a"}) == 2);

    // And a link signed by somebody else is refused exactly like any other forged record.
    Record forged = *stored;
    forged.writer = key_id('a');
    forged.sig = mallory.sign(record_hash(forged, hasher));
    CHECK(replica.ingest(forged) != IngestResult::Accepted);
}

TEST_CASE("the chain is followed from a trusted root, one link at a time") {
    Sha256Hasher hasher;
    InsecureTestSigner alice(key_id('a')), bob(key_id('b')), carol(key_id('c'));
    MemoryLogStore store;

    append_payload(store, alice, hasher, "events.a", "mine");
    rotate(store, alice, hasher, "events.a", bob.id());
    append_payload(store, bob, hasher, "events.a", "bob's");
    rotate(store, bob, hasher, "events.a", carol.id());

    RotationChain chain(store, alice);
    const auto resolved = chain.resolve(key_id('a'), "events.a");
    CHECK_FALSE(resolved.refused);
    REQUIRE(resolved.identities.size() == 3);
    CHECK(resolved.identities[0] == key_id('a'));
    CHECK(resolved.identities[1] == key_id('b'));
    CHECK(resolved.identities[2] == key_id('c'));

    // A different channel is a different log, so rotation on one does not leak to the other.
    const auto other = chain.resolve(key_id('a'), "events.other");
    REQUIRE(other.identities.size() == 1);  // rotation is per channel, per log
}

TEST_CASE("a cycle, a repeat and a too-long chain all fail closed") {
    Sha256Hasher hasher;
    InsecureTestSigner alice(key_id('a')), bob(key_id('b'));
    MemoryLogStore store;

    // A rotates to B and B rotates back to A.
    rotate(store, alice, hasher, "loop", bob.id());
    rotate(store, bob, hasher, "loop", alice.id());
    RotationChain chain(store, alice);
    const auto cycle = chain.resolve(key_id('a'), "loop");
    CHECK(cycle.refused);
    CHECK(cycle.identities.size() <= 2);  // it stops where it repeats

    // One link per identity, more links than the depth limit allows: the walk fails closed rather
    // than treating "I stopped looking" as "there is nothing more".
    InsecureTestSigner keys[5] = {InsecureTestSigner(key_id('1')), InsecureTestSigner(key_id('2')),
                                  InsecureTestSigner(key_id('3')), InsecureTestSigner(key_id('4')),
                                  InsecureTestSigner(key_id('5'))};
    MemoryLogStore deep;
    for (int i = 0; i + 1 < 5; ++i)
        rotate(deep, keys[i], hasher, "deep", keys[i + 1].id());
    RotationChain bounded(deep, alice, 2);
    const auto too_long = bounded.resolve(key_id('1'), "deep");
    CHECK(too_long.refused);
    CHECK(too_long.reason == string("chain longer than the configured depth"));
}

TEST_CASE("a link signed by the wrong key, or in someone else's log, grants nothing") {
    Sha256Hasher hasher;
    InsecureTestSigner alice(key_id('a')), bob(key_id('b')), mallory(key_id('d'));
    MemoryLogStore store;

    // A record that claims to be alice's link but is signed by mallory: the store keeps it, the
    // chain does not believe it.
    RotationLink claim;
    claim.successor = mallory.id();
    Record record;
    record.writer = key_id('a');
    record.channel = "events.a";
    record.hash_algo = hasher.algorithm();
    record.seq = 0;
    record.content_type = kRotationContentType;
    record.payload = encode_rotation_link(claim);
    record.sig = mallory.sign(record_hash(record, hasher));
    REQUIRE(store.append(record) == AppendResult::Ok);

    RotationChain chain(store, alice);
    const auto resolved = chain.resolve(key_id('a'), "events.a");
    REQUIRE(resolved.identities.size() == 1);
    CHECK(resolved.identities[0] == key_id('a'));

    // A stranger claiming to *succeed* alice from its own log is likewise nothing: the claim has to
    // live in the log it is about.
    rotate(store, bob, hasher, "events.a", mallory.id());
    const auto from_stranger = chain.resolve(key_id('a'), "events.a");
    CHECK(from_stranger.identities.size() == 1);
}

TEST_CASE("a cached chain is dropped when a link arrives") {
    Sha256Hasher hasher;
    InsecureTestSigner alice(key_id('a')), bob(key_id('b'));
    MemoryLogStore store;
    append_payload(store, alice, hasher, "events.a", "before");

    RotationChain chain(store, alice);
    REQUIRE(chain.resolve(key_id('a'), "events.a").identities.size() == 1);

    rotate(store, alice, hasher, "events.a", bob.id());
    // Still one: the answer is cached, and a test that skipped this would hide a cache bug.
    CHECK(chain.resolve(key_id('a'), "events.a").identities.size() == 1);

    chain.invalidate(key_id('a'), "events.a");  // what ingest does when it stores a link
    const auto after = chain.resolve(key_id('a'), "events.a");
    REQUIRE(after.identities.size() == 2);
    CHECK(after.identities[1] == key_id('b'));
}

TEST_CASE("a rule that names a root authorizes its successors, and never the other way round") {
    Sha256Hasher hasher;
    InsecureTestSigner alice(key_id('a')), bob(key_id('b'));
    MemoryLogStore store;
    append_payload(store, alice, hasher, "svc.jobs", "before");
    rotate(store, alice, hasher, "svc.jobs", bob.id());
    RotationChain chain(store, alice);

    ChannelRules rooted;
    REQUIRE(rooted.allow_rotation("svc.jobs", {alice.id()}));
    CHECK(rooted.permits("svc.jobs", alice.id(), chain));         // the root itself
    CHECK(rooted.permits("svc.jobs", bob.id(), chain));           // and its successor
    CHECK_FALSE(rooted.permits("svc.jobs", key_id('e'), chain));  // an unrelated key gains nothing
    // Without a chain, a rotation root authorizes only itself: "I cannot check" is not "trusted".
    CHECK(rooted.permits("svc.jobs", alice.id()));
    CHECK_FALSE(rooted.permits("svc.jobs", bob.id()));

    // A rotation authorizes the successor, never the predecessor: retiring a key by rotating away
    // from it has to retire something.
    ChannelRules successor_only;
    REQUIRE(successor_only.allow("svc.jobs", {bob.id()}));
    CHECK_FALSE(successor_only.permits("svc.jobs", alice.id(), chain));
    CHECK(successor_only.permits("svc.jobs", bob.id(), chain));

    // An open channel stays open: adding rotation must not quietly close anything.
    ChannelRules open;
    CHECK(open.permits("anywhere", key_id('e'), chain));
}

TEST_CASE("a fork in the successor's log is still reported") {
    Sha256Hasher hasher;
    InsecureTestSigner alice(key_id('a')), bob(key_id('b'));
    MemoryLogStore writer_store, replica_store;

    // A's log hands over to B, and B then equivocates on its first record.
    rotate(writer_store, alice, hasher, "events.a", bob.id());
    LogWriter bob_writer(writer_store, hasher, bob, "events.a");
    const auto first = bob_writer.append("text/plain", text("bob's first"), 1000);

    ReplicaLog replica(replica_store, hasher, alice);
    REQUIRE(replica.ingest(*writer_store.get({key_id('a'), "events.a"}, 0)) ==
            IngestResult::Accepted);
    REQUIRE(replica.ingest(first) == IngestResult::Accepted);
    CHECK(replica.forks().empty());

    // The same sequence, different history, signed by the successor key: the evidence is recorded
    // against B's log exactly as it would be for any writer - rotation changes nothing about it.
    Record fork = first;
    fork.content_type = "text/plain";
    fork.payload = text("a different history");
    fork.sig = bob.sign(record_hash(fork, hasher));
    CHECK(replica.ingest(fork) == IngestResult::Fork);
    REQUIRE(replica.forks().size() == 1);
    CHECK(replica.forks().front().key.writer == bob.id());
}
