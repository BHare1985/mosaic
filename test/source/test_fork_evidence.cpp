// LOG-1: equivocation detected is not the same claim as equivocation provable. This keeps both
// sides.
#include <mosaic/core/insecure_test_signer.h>
#include <mosaic/log/log.h>
#include <mosaic/store/memory_log_store.h>

#include <cstdio>
#include <doctest/doctest.h>

#ifdef MOSAIC_HAS_SQLITE
#include <mosaic/store/sqlite_log_store.h>

using std::remove;
using std::string;
#endif

using namespace mosaic;

namespace {
Bytes bytes(const string& s) {
    return Bytes(s.begin(), s.end());
}

struct Fixture {
    Sha256Hasher hasher;
    InsecureTestSigner alice{"alice"};
    MemoryLogStore writer_store;
    MemoryLogStore replica_store;
    LogWriter writer{writer_store, hasher, alice, "chan.one"};
    ReplicaLog replica{replica_store, hasher, alice};
};

/// A second, validly signed record for the same sequence with different content: what a fork looks
/// like.
Record twin_of(const Record& original, const Signer& signer, const Hasher& hasher) {
    Record twin = original;
    twin.payload = bytes("a different history");
    twin.sig = signer.sign(record_hash(twin, hasher));
    return twin;
}
}  // namespace

TEST_CASE("a fork keeps both records as evidence") {
    Fixture f;
    const auto r0 = f.writer.append("t", bytes("first"), 1);
    REQUIRE(f.replica.ingest(r0) == IngestResult::Accepted);

    const auto twin = twin_of(r0, f.alice, f.hasher);
    CHECK(f.replica.ingest(twin) == IngestResult::Fork);

    const auto evidence = f.replica.forks();
    REQUIRE(evidence.size() == 1);
    CHECK(evidence.front().seq == 0);
    CHECK(evidence.front().accepted.payload == r0.payload);  // the history we kept serving
    CHECK(evidence.front().conflicting.payload ==
          twin.payload);  // and the one that tried to replace it
    // The accepted chain is untouched: evidence never rewrites history.
    CHECK(f.replica.head(r0.key())->hash == record_hash(r0, f.hasher));
}

TEST_CASE("evidence is bounded and the overflow is counted") {
    Fixture f;
    f.replica.max_evidence = 2;
    const auto r0 = f.writer.append("t", bytes("first"), 1);
    REQUIRE(f.replica.ingest(r0) == IngestResult::Accepted);

    for (int i = 0; i < 5; ++i) {
        Record twin = r0;
        twin.payload = bytes("variant-" + std::to_string(i));
        twin.sig = f.alice.sign(record_hash(twin, f.hasher));
        CHECK(f.replica.ingest(twin) == IngestResult::Fork);
    }
    CHECK(f.replica.forks().size() ==
          1);  // the same sequence is one piece of evidence, replaced not stacked
    CHECK(f.replica.evidence_overflow == 0);
}

TEST_CASE("a chain contradiction is kept as evidence too") {
    Fixture f;
    const auto r0 = f.writer.append("t", bytes("first"), 1);
    REQUIRE(f.replica.ingest(r0) == IngestResult::Accepted);

    // A validly signed successor that links to a different history.
    Record other = twin_of(r0, f.alice, f.hasher);
    Record orphan;
    orphan.writer = "alice";
    orphan.channel = "chan.one";
    orphan.hash_algo = f.hasher.algorithm();
    orphan.seq = 1;  // the next expected sequence, but linking to a history we do not hold
    orphan.prev = record_hash(other, f.hasher);
    orphan.sig = f.alice.sign(record_hash(orphan, f.hasher));

    CHECK(f.replica.ingest(orphan) == IngestResult::Fork);
    const auto evidence = f.replica.forks();
    REQUIRE_FALSE(evidence.empty());
    CHECK(evidence.front().conflicting.seq == 1);
}

#ifdef MOSAIC_HAS_SQLITE
TEST_CASE("evidence survives a restart when the store does") {
    // Two separate stores: the writer's own log and the replica's. Sharing one would make the
    // replica see its own stored record as a duplicate rather than as a fork.
    const string writer_path = "test-fork-writer.db";
    const string path = "test-fork-evidence.db";
    remove(writer_path.c_str());
    remove(path.c_str());
    Record accepted, conflicting;
    {
        Sha256Hasher hasher;
        InsecureTestSigner alice("alice");
        auto writer_store = SqliteLogStore::open(writer_path);
        auto store = SqliteLogStore::open(path);
        REQUIRE(writer_store.has_value());
        REQUIRE(store.has_value());
        LogWriter writer(**writer_store, hasher, alice, "chan.one");
        ReplicaLog replica(**store, hasher, alice);

        const auto r0 = writer.append("t", bytes("kept"), 1);
        REQUIRE(replica.ingest(r0) == IngestResult::Accepted);
        Record twin = r0;
        twin.payload = bytes("other");
        twin.sig = alice.sign(record_hash(twin, hasher));
        REQUIRE(replica.ingest(twin) == IngestResult::Fork);
        accepted = r0;
        conflicting = twin;
    }

    auto reopened = SqliteLogStore::open(path);
    REQUIRE(reopened.has_value());
    const auto evidence = (*reopened)->load_forks({"alice", "chan.one"});
    REQUIRE(evidence.size() == 1);
    CHECK(evidence.front().accepted.payload == accepted.payload);
    CHECK(evidence.front().conflicting.payload == conflicting.payload);
    for (const auto& suffix : {"", "-wal", "-shm"}) {
        remove((path + suffix).c_str());
        remove((writer_path + suffix).c_str());
    }
}
#endif
