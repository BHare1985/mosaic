#include <mosaic/core/insecure_test_signer.h>
#include <mosaic/log/log.h>
#include <mosaic/store/memory_log_store.h>

#include <doctest/doctest.h>

using std::string;

using namespace mosaic;

namespace {
Bytes bytes(const string& s) {
    return Bytes(s.begin(), s.end());
}

struct Fixture {
    Sha256Hasher hasher;
    InsecureTestSigner alice{"alice"};
    MemoryLogStore writer_store, replica_store;
    LogWriter writer{writer_store, hasher, alice, "chan.one"};
    ReplicaLog replica{replica_store, hasher, alice};
};
}  // namespace

TEST_CASE_FIXTURE(Fixture, "writer builds a hash chain") {
    const auto r0 = writer.append("t", bytes("a"), 1);
    const auto r1 = writer.append("t", bytes("b"), 2);
    CHECK(r0.seq == 0);
    CHECK(r0.prev == Hash{});
    CHECK(r1.seq == 1);
    CHECK(r1.prev == record_hash(r0, hasher));
    CHECK(writer.head()->seq == 1);
    CHECK(writer.head()->hash == record_hash(r1, hasher));
}

TEST_CASE_FIXTURE(Fixture, "writer resumes from the store") {
    writer.append("t", bytes("a"), 1);
    LogWriter again{writer_store, hasher, alice, "chan.one"};
    CHECK(again.append("t", bytes("b"), 2).seq == 1);
}

TEST_CASE_FIXTURE(Fixture, "replica accepts in order, reports duplicates and gaps") {
    const auto r0 = writer.append("t", bytes("a"), 1);
    const auto r1 = writer.append("t", bytes("b"), 2);
    const auto r2 = writer.append("t", bytes("c"), 3);

    CHECK(replica.ingest(r1) == IngestResult::Gap);
    CHECK(replica.ingest(r0) == IngestResult::Accepted);
    CHECK(replica.ingest(r0) == IngestResult::Duplicate);
    CHECK(replica.ingest(r2) == IngestResult::Gap);
    CHECK(replica.ingest(r1) == IngestResult::Accepted);
    CHECK(replica.ingest(r2) == IngestResult::Accepted);
    CHECK(replica.head(r0.key())->seq == 2);
    CHECK(replica.head(r0.key())->hash == writer.head()->hash);
}

TEST_CASE_FIXTURE(Fixture, "replica rejects tampering and forgery") {
    auto r0 = writer.append("t", bytes("a"), 1);

    Record tampered = r0;
    tampered.payload = bytes("evil");
    CHECK(replica.ingest(tampered) == IngestResult::BadSignature);

    Record forged = r0;
    forged.writer = "mallory";  // signature was made for "alice"
    CHECK(replica.ingest(forged) == IngestResult::BadSignature);

    CHECK_FALSE(replica.head(r0.key()).has_value());
}

TEST_CASE_FIXTURE(Fixture, "replica detects equivocation (fork)") {
    const auto r0 = writer.append("t", bytes("a"), 1);
    REQUIRE(replica.ingest(r0) == IngestResult::Accepted);

    // The same writer signs a different record 0: validly signed, different content.
    Record twin = r0;
    twin.payload = bytes("b");
    twin.sig = alice.sign(record_hash(twin, hasher));
    CHECK(replica.ingest(twin) == IngestResult::Fork);

    // A record 1 that chains to a different record 0 is also a fork.
    Record r1;
    r1.writer = "alice";
    r1.channel = "chan.one";
    r1.hash_algo = hasher.algorithm();
    r1.seq = 1;
    r1.prev = record_hash(twin, hasher);
    r1.sig = alice.sign(record_hash(r1, hasher));
    CHECK(replica.ingest(r1) == IngestResult::Fork);
}

// docs/design/01-data-model.md fixes the order of the checks: signature, then stored history, then
// gap, then link. A record that violates two rules must therefore be classified by the first one,
// otherwise two replicas disagree about the same bytes.
TEST_CASE_FIXTURE(Fixture, "verification order: signature is checked before structure") {
    const auto r0 = writer.append("t", bytes("a"), 1);
    REQUIRE(replica.ingest(r0) == IngestResult::Accepted);

    // Forgery *and* gap: the expected next sequence is 1, so seq 5 would be a gap.
    Record forged_gap = r0;
    forged_gap.seq = 5;
    forged_gap.prev = record_hash(r0, hasher);
    CHECK(replica.ingest(forged_gap) == IngestResult::BadSignature);

    // Forgery *and* bad chain: seq 0 with a non-zero prev is BadChain on its own.
    Record forged_chain;
    forged_chain.writer = "alice";
    forged_chain.channel = "chan.one";
    forged_chain.hash_algo = hasher.algorithm();
    forged_chain.prev.bytes[0] = 1;
    forged_chain.sig = bytes("not-a-signature");
    CHECK(replica.ingest(forged_chain) == IngestResult::BadSignature);

    // Neither rejection may have touched the stored log.
    CHECK(replica.head(r0.key())->seq == 0);
}

TEST_CASE_FIXTURE(Fixture, "seq 0 must have zero prev") {
    Record r;
    r.writer = "alice";
    r.channel = "chan.one";
    r.hash_algo = hasher.algorithm();
    r.prev.bytes[0] = 1;
    r.sig = alice.sign(record_hash(r, hasher));
    CHECK(replica.ingest(r) == IngestResult::BadChain);
}
