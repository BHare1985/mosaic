// Batched ingest: a repair reply of N records should cost one store call, with exactly the outcomes
// one-at-a-time would have produced (spec: log/replicated-log, LOG-2).
#include <mosaic/core/insecure_test_signer.h>
#include <mosaic/log/log.h>
#include <mosaic/store/memory_log_store.h>

#include <chrono>
#include <doctest/doctest.h>
#include <string>
#include <vector>

namespace chrono = std::chrono;
using std::string;
using std::vector;

using namespace mosaic;

namespace {
Bytes text(const string& s) {
    return Bytes(s.begin(), s.end());
}

/// A writer whose log lives in `store`, so a test can produce real signed chains.
struct Chain {
    InsecureTestSigner signer;
    Sha256Hasher hasher;
    explicit Chain(const char* id) : signer(id) {}

    vector<Record> make(MemoryLogStore& store, const string& channel, int count,
                        uint64_t at_ms = 1000) {
        LogWriter writer(store, hasher, signer, channel);
        vector<Record> out;
        for (int i = 0; i < count; ++i)
            out.push_back(writer.append("text/plain", text("r" + std::to_string(i)), at_ms));
        return out;
    }
};

/// Everything a replica holds, as one comparable value.
vector<string> contents(const MemoryLogStore& store, const LogKey& key) {
    vector<string> out;
    const auto head = store.head_seq(key);
    if (!head)
        return out;
    for (Seq seq = 0; seq <= *head; ++seq) {
        const auto record = store.get(key, seq);
        if (record)
            out.push_back(std::to_string(seq) + ":" +
                          std::string(record->payload.begin(), record->payload.end()));
    }
    return out;
}
}  // namespace

TEST_CASE("a batch produces exactly the outcomes one-at-a-time would") {
    InsecureTestSigner alice("alice");
    Chain chain("alice");
    Sha256Hasher hasher;
    MemoryLogStore writer_store, one_by_one, batched;

    auto records = chain.make(writer_store, "events.a", 12);
    // A batch a repair would actually produce: in sequence order, with a duplicate the link made.
    // (A refusal *ends* a run, so equivalence with one-at-a-time only holds up to the first one;
    // that boundary has its own test below.)
    records.insert(records.begin() + 4, records[3]);

    ReplicaLog step(one_by_one, hasher, alice);
    vector<IngestResult> individual;
    for (const auto& record : records)
        individual.push_back(step.ingest(record));

    ReplicaLog once(batched, hasher, alice);
    const auto together = once.ingest_batch(records);

    REQUIRE(together.size() == individual.size());
    for (size_t i = 0; i < records.size(); ++i)
        CHECK(together[i] == individual[i]);
    CHECK(contents(one_by_one, {"alice", "events.a"}) == contents(batched, {"alice", "events.a"}));
    CHECK(contents(one_by_one, {"alice", "events.b"}) == contents(batched, {"alice", "events.b"}));
    CHECK(step.forks().size() == once.forks().size());
}

TEST_CASE("a gap stops the run, and the rest is reported as not attempted") {
    InsecureTestSigner alice("alice");
    Chain chain("alice");
    Sha256Hasher hasher;
    MemoryLogStore writer_store, replica_store;
    const auto records = chain.make(writer_store, "events.a", 6);

    ReplicaLog replica(replica_store, hasher, alice);
    // Offer 0, 1, then jump to 4: the run closes at the gap and 5 is not attempted.
    const vector<Record> batch{records[0], records[1], records[4], records[5]};
    const auto outcomes = replica.ingest_batch(batch);
    REQUIRE(outcomes.size() == 4);
    CHECK(outcomes[0] == IngestResult::Accepted);
    CHECK(outcomes[1] == IngestResult::Accepted);
    CHECK(outcomes[2] == IngestResult::Gap);
    CHECK(outcomes[3] == IngestResult::NotAttempted);
    CHECK(replica_store.head_seq({"alice", "events.a"}) == 1);

    // The repair that closes the gap: 2, 3, then 4 and 5 as one run.
    const vector<Record> repair{records[2], records[3], records[4], records[5]};
    const auto second = replica.ingest_batch(repair);
    for (const auto outcome : second)
        CHECK(outcome == IngestResult::Accepted);
    CHECK(replica_store.head_seq({"alice", "events.a"}) == 5);
}

TEST_CASE("two logs interleaved in one batch keep their own order") {
    InsecureTestSigner alice("alice");
    Chain chain("alice");
    Sha256Hasher hasher;
    MemoryLogStore writer_store, replica_store;
    const auto a = chain.make(writer_store, "events.a", 4);
    const auto b = chain.make(writer_store, "events.b", 4);

    vector<Record> interleaved;
    for (int i = 0; i < 4; ++i) {
        interleaved.push_back(a[static_cast<size_t>(i)]);
        interleaved.push_back(b[static_cast<size_t>(i)]);
    }

    ReplicaLog replica(replica_store, hasher, alice);
    const auto outcomes = replica.ingest_batch(interleaved);
    for (const auto outcome : outcomes)
        CHECK(outcome == IngestResult::Accepted);  // each log sees its own sequence order
    CHECK(replica_store.head_seq({"alice", "events.a"}) == 3);
    CHECK(replica_store.head_seq({"alice", "events.b"}) == 3);
    CHECK(contents(replica_store, {"alice", "events.a"}).size() == 4);
    CHECK(contents(replica_store, {"alice", "events.b"}).size() == 4);
}

TEST_CASE("a fork inside a batch is still evidence") {
    InsecureTestSigner alice("alice");
    Chain chain("alice");
    Sha256Hasher hasher;
    MemoryLogStore writer_store, replica_store;
    auto records = chain.make(writer_store, "events.a", 4);

    ReplicaLog replica(replica_store, hasher, alice);
    REQUIRE(replica.ingest_batch({records[0], records[1], records[2]})[2] ==
            IngestResult::Accepted);

    // The same sequence with a different history, now that the original is stored.
    Record fork = records[2];
    fork.payload = text("a different history");
    fork.sig = alice.sign(record_hash(fork, hasher));
    const auto outcomes = replica.ingest_batch({fork, records[3]});
    CHECK(outcomes[0] == IngestResult::Fork);
    CHECK(outcomes[1] == IngestResult::NotAttempted);
    CHECK(replica.forks().size() == 1);
}

TEST_CASE("batched ingest is measured, not assumed") {
    // A repair-sized batch, through the same store, one record at a time against a batch. Measured
    // here rather than asserted: the number is recorded in performance/measured-cost with the
    // machine it came from.
    InsecureTestSigner alice("alice");
    Chain chain("alice");
    Sha256Hasher hasher;
    MemoryLogStore writer_store;
    auto records = chain.make(writer_store, "events.a", 256);
    MemoryLogStore store;

    const auto start = chrono::steady_clock::now();
    ReplicaLog replica(store, hasher, alice);
    auto outcomes = replica.ingest_batch(records);
    const auto elapsed = chrono::steady_clock::now() - start;
    REQUIRE(outcomes.back() == IngestResult::Accepted);
    const auto per_record = chrono::duration_cast<chrono::nanoseconds>(elapsed).count() /
                            static_cast<long long>(records.size());
    MESSAGE("batch ingest: " << records.size() << " records -> " << per_record << " ns each");
    CHECK(per_record > 0);
}
