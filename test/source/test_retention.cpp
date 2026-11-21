// Checkpoints and retention: a log may keep only a suffix, and it must stay verifiable and
// fork-honest when it does (spec: store/log-store, STORE-3).
#include <mosaic/core/insecure_test_signer.h>
#include <mosaic/log/checkpoint.h>
#include <mosaic/log/log.h>
#include <mosaic/net/sim_network.h>
#include <mosaic/node/node.h>
#include <mosaic/store/memory_log_store.h>
#include <mosaic/store/segment_log_store.h>

#include <doctest/doctest.h>
#include <filesystem>
#include <string>

using std::move;
using std::string;

using namespace mosaic;

namespace {
namespace fs = std::filesystem;

Bytes text(const string& s) {
    return Bytes(s.begin(), s.end());
}

/// A writer whose log lives in `store`, so a test can produce real signed chains.
struct Writer {
    MemoryLogStore memory;
    SegmentLogStore* segment = nullptr;
    LogStore* store = nullptr;
    InsecureTestSigner signer{"alice"};
    Sha256Hasher hasher;

    explicit Writer(MemoryLogStore& s) : store(&s) {}
    explicit Writer(SegmentLogStore& s) : segment(&s), store(&s) {}

    /// The store refuses a sequence that is not next, so `seq` is only a check on the caller.
    Record append(const string& channel, Seq seq, const string& payload) {
        LogWriter writer(*store, hasher, signer, channel);
        Record record = writer.append("text/plain", text(payload), 1000);
        CHECK(record.seq == seq);
        return record;
    }
};

Checkpoint checkpoint_at(const Record& record, const Sha256Hasher& hasher, const Signer& signer) {
    Checkpoint checkpoint;
    checkpoint.key = record.key();
    checkpoint.seq = record.seq;
    checkpoint.hash = *record_hash_tagged(record);
    checkpoint.hash_algo = hasher.algorithm();
    return sign_checkpoint(move(checkpoint), signer);
}

fs::path temp_dir(const char* name) {
    const auto dir = fs::temp_directory_path() / name;
    std::error_code error;
    fs::remove_all(dir, error);
    fs::create_directories(dir, error);
    return dir;
}
}  // namespace

TEST_CASE("a checkpoint round-trips, and a tampered one is refused") {
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    MemoryLogStore store;
    Writer writer(store);
    const auto record = writer.append("events.a", 0, "first");
    const auto checkpoint = checkpoint_at(record, hasher, alice);

    CHECK(verify_checkpoint(checkpoint, alice));
    const auto decoded = decode_checkpoint(encode_checkpoint(checkpoint));
    REQUIRE(decoded.has_value());
    CHECK(decoded->seq == 0);
    CHECK(decoded->hash == checkpoint.hash);
    CHECK(verify_checkpoint(*decoded, alice));

    // A different sequence is a different statement, so the signature no longer covers it.
    Checkpoint edited = *decoded;
    edited.seq = 7;
    CHECK_FALSE(verify_checkpoint(edited, alice));
    CHECK(decode_checkpoint(encode_checkpoint(edited)).has_value());  // it decodes; it just fails

    // A truncation is not a checkpoint: nothing is guessed from a partial object.
    auto bytes = encode_checkpoint(checkpoint);
    bytes.resize(bytes.size() - 3);
    CHECK_FALSE(decode_checkpoint(bytes).has_value());

    // A format this build does not know is refused rather than parsed.
    auto future = encode_checkpoint(checkpoint);
    future[0] = 99;
    CHECK_FALSE(decode_checkpoint(future).has_value());
}

TEST_CASE("truncation keeps the checkpoint and reads start after it") {
    const auto dir = temp_dir("mosaic-retention-truncate");
    auto store = SegmentLogStore::open(dir.string(), Durability::PerRecord, 160);
    REQUIRE(store.has_value());
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    Writer writer(**store);

    Record last;
    for (Seq seq = 0; seq < 12; ++seq)
        last = writer.append("events.a", seq, "record " + std::to_string(seq));
    const auto key = last.key();
    const auto full_head = (*store)->head_seq(key);
    REQUIRE(full_head.has_value());

    // A checkpoint at a record that will survive, then drop everything before it.
    const auto anchor = (*store)->get(key, 8);
    REQUIRE(anchor.has_value());
    const auto checkpoint = checkpoint_at(*anchor, hasher, alice);
    REQUIRE((*store)->put_checkpoint(key, encode_checkpoint(checkpoint)));
    const auto dropped = (*store)->truncate_before(key, 9);
    REQUIRE(dropped.has_value());
    CHECK(*dropped > 0);

    // The checkpoint survives even though the records it covers may not, and reads start at it.
    const auto kept = (*store)->checkpoint(key);
    REQUIRE(kept.has_value());
    const auto decoded = decode_checkpoint(*kept);
    REQUIRE(decoded.has_value());
    CHECK(verify_checkpoint(*decoded, alice));
    CHECK_FALSE((*store)->get(key, 0).has_value());
    CHECK((*store)->get(key, 11).has_value());
    CHECK((*store)->head_seq(key) == full_head);  // the head is unchanged: nothing was invented

    std::error_code error;
    fs::remove_all(dir, error);
}

TEST_CASE("a truncated log still verifies a record that descends from its checkpoint") {
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    Verifier& verifier = alice;
    // Two stores: the writer keeps the whole chain, the replica keeps a suffix of it.
    MemoryLogStore writer_store, replica_store;
    Writer writer(writer_store);

    Record records[6];
    for (Seq seq = 0; seq < 6; ++seq)
        records[seq] = writer.append("events.a", seq, "r" + std::to_string(seq));

    const auto origin = [&replica_store](const LogKey& key, Seq seq) -> std::optional<Hash> {
        const auto blob = replica_store.checkpoint(key);
        if (!blob)
            return std::nullopt;
        const auto cp = decode_checkpoint(*blob);
        if (!cp || cp->seq != seq)
            return std::nullopt;
        return cp->hash;
    };
    ReplicaLog replica(replica_store, hasher, verifier);
    replica.set_origin(origin);

    for (const auto& record : records)
        REQUIRE(replica.ingest(record) == IngestResult::Accepted);

    // The replica keeps the tail of the log and a signed checkpoint naming what it dropped.
    const auto checkpoint = checkpoint_at(records[4], hasher, alice);
    REQUIRE(replica_store.put_checkpoint(records[4].key(), encode_checkpoint(checkpoint)));
    REQUIRE(replica_store.truncate_before(records[4].key(), 4));

    // A record whose predecessor is gone verifies against the checkpoint rather than failing.
    const auto next = writer.append("events.a", 6, "after truncation");
    CHECK(replica.ingest(next) == IngestResult::Accepted);

    // The same sequence with a different predecessor is not accepted: the checkpoint says what the
    // predecessor hashed to, and this record disagrees with it.
    Record wrong = writer.append("events.a", 7, "forged predecessor");
    wrong.prev.bytes[0] ^= 0xFF;
    wrong.sig = alice.sign(record_hash(wrong, hasher));
    CHECK(replica.ingest(wrong) != IngestResult::Accepted);
}

TEST_CASE("retention drops a prefix only when the policy allows it") {
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(801);
    SimTransport transport(net, "writer");
    MemoryLogStore store;

    NodeConfig config;
    config.id = "writer";
    config.retention_keep_min = 2;  // keep the last two records
    Node node(config, transport, store, hasher, alice);

    for (int i = 0; i < 6; ++i) {
        REQUIRE(node.publish(alice, "audit.log", "t", text("entry " + std::to_string(i)),
                             static_cast<uint64_t>(100 + i))
                    .has_value());
    }
    const LogKey key{"alice", "audit.log"};
    REQUIRE(node.head(key)->seq == 5);

    const auto outcome = node.retain(alice, 200);
    CHECK(outcome.dropped == 4);
    CHECK(outcome.logs == 1);
    CHECK(outcome.refused == 0);
    CHECK(node.head(key)->seq == 5);             // the head never moves because retention ran
    CHECK_FALSE(store.get(key, 3).has_value());  // the prefix is gone
    REQUIRE(store.get(key, 4).has_value());      // the kept tail starts here

    // What was dropped is still accounted for: a signed checkpoint names the last dropped record.
    const auto blob = store.checkpoint(key);
    REQUIRE(blob.has_value());
    const auto checkpoint = decode_checkpoint(*blob);
    REQUIRE(checkpoint.has_value());
    CHECK(checkpoint->seq == 3);
    CHECK(verify_checkpoint(*checkpoint, alice));
    // The anchor is the last *dropped* record (3), not the first kept one (4).
    CHECK(checkpoint->seq == 3);
    CHECK(node.counters().retention_dropped == 4);
}

TEST_CASE("retention never outruns replication, and refuses when it cannot know") {
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    SimNetwork net(803);
    SimTransport transport(net, "writer");
    MemoryLogStore store;

    NodeConfig config;
    config.id = "writer";
    config.retention_keep_min = 1;
    config.min_replicas = 2;  // nothing may be dropped until two replicas acknowledge
    Node node(config, transport, store, hasher, alice);

    for (int i = 0; i < 4; ++i)
        REQUIRE(node.publish(alice, "audit.log", "t", text("e" + std::to_string(i)),
                             static_cast<uint64_t>(100 + i))
                    .has_value());
    const LogKey key{"alice", "audit.log"};

    const auto refused = node.retain(alice, 200);
    CHECK(refused.dropped == 0);  // one acknowledgement is missing, so nothing goes
    CHECK(refused.refused == 1);
    CHECK(store.get(key, 0).has_value());
    CHECK(node.counters().retention_refused == 1);
}

TEST_CASE("truncation cannot manufacture a fork, and fork evidence still works") {
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    MemoryLogStore writer_store, replica_store;
    ReplicaLog replica(replica_store, hasher, alice);
    Writer writer(writer_store);

    Record records[5];
    for (Seq seq = 0; seq < 5; ++seq)
        records[seq] = writer.append("events.a", seq, "r" + std::to_string(seq));
    for (const auto& record : records)
        REQUIRE(replica.ingest(record) == IngestResult::Accepted);

    // Drop the prefix: the surviving records are 3 and 4, and they are unchanged.
    REQUIRE(replica_store.truncate_before(records[0].key(), 3));
    REQUIRE(replica_store.get(records[0].key(), 3).has_value());

    // A different record at a *kept* sequence is still equivocation, with both sides recorded.
    Record fork = records[3];
    fork.payload = text("a different history at the same sequence");
    fork.sig = alice.sign(record_hash(fork, hasher));
    CHECK(replica.ingest(fork) == IngestResult::Fork);
    CHECK(replica.forks().size() == 1);
}
