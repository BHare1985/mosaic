// CLI-2's logic, tested in the library: a store either verifies or names where it stops being true.
#include <doctest/doctest.h>
#include <mosaic/core/insecure_test_signer.h>
#include <mosaic/log/log.h>
#include <mosaic/log/verify.h>
#include <mosaic/store/memory_log_store.h>

using namespace mosaic;

namespace {
Bytes bytes(const std::string& s) { return Bytes(s.begin(), s.end()); }

/// Builds a valid log of `count` records signed by alice.
void fill(MemoryLogStore& store, const Hasher& hasher, const Signer& signer, int count)
{
    LogWriter writer(store, hasher, signer, "chan.one");
    for (int i = 0; i < count; ++i)
        writer.append("t", bytes(std::to_string(i)), static_cast<std::uint64_t>(i + 1));
}

/// A store that hands back whatever it is given: the way to inject exactly one kind of damage.
class RiggedStore final : public LogStore {
public:
    explicit RiggedStore(std::vector<Record> records, std::optional<Seq> claimed_head = std::nullopt)
        : records_(std::move(records))
        , claimed_head_(claimed_head)
    {
    }

    AppendResult append(const Record&) override { return AppendResult::NotNext; }
    std::optional<Seq> head_seq(const LogKey&) const override
    {
        if (claimed_head_)
            return claimed_head_; // a store that claims more than it can serve
        return records_.empty() ? std::nullopt : std::optional<Seq>(records_.back().seq);
    }
    std::optional<Record> get(const LogKey&, Seq seq) const override
    {
        for (const auto& record : records_)
            if (record.seq == seq)
                return record;
        return std::nullopt;
    }
    std::vector<Record> read(const LogKey&, Seq from, std::size_t count) const override
    {
        std::vector<Record> out;
        for (const auto& record : records_)
            if (record.seq >= from && out.size() < count)
                out.push_back(record);
        return out;
    }
    std::vector<LogKey> keys() const override
    {
        if (records_.empty())
            return { };
        return { records_.front().key() };
    }

private:
    std::vector<Record> records_;
    std::optional<Seq> claimed_head_;
};

std::vector<Record> good_records()
{
    MemoryLogStore store;
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    fill(store, hasher, alice, 4);
    return store.read({ "alice", "chan.one" }, 0, 100);
}
} // namespace

TEST_CASE("an intact store verifies and reports what it checked")
{
    MemoryLogStore store;
    Sha256Hasher hasher;
    InsecureTestSigner alice("alice");
    fill(store, hasher, alice, 4);

    const auto report = verify_store(store, alice);
    CHECK(report.ok());
    CHECK(report.logs == 1);
    CHECK(report.records == 4);
}

TEST_CASE("an altered payload is caught at its sequence")
{
    auto records = good_records();
    records[2].payload = bytes("tampered");
    RiggedStore store(records);

    InsecureTestSigner alice("alice");
    const auto report = verify_store(store, alice);
    REQUIRE_FALSE(report.ok());
    CHECK(report.failure->seq == 2);
    CHECK(report.failure->reason.find("signature") != std::string::npos);
    CHECK(report.records == 2); // everything before the damage still counted
}

TEST_CASE("a gap is a failure, not a short log")
{
    auto records = good_records();
    records.erase(records.begin() + 2); // sequences 0,1,3
    RiggedStore store(records);

    InsecureTestSigner alice("alice");
    const auto report = verify_store(store, alice);
    REQUIRE_FALSE(report.ok());
    CHECK(report.failure->seq == 2);
    CHECK(report.failure->reason.find("gap") != std::string::npos);
}

TEST_CASE("a broken link is caught")
{
    auto records = good_records();
    records[3].prev.bytes[0] ^= 0xff; // keep the signature valid? no - resign so the link is the only fault
    records[3].sig = InsecureTestSigner("alice").sign(record_hash(records[3], Sha256Hasher { }));
    RiggedStore store(records);

    InsecureTestSigner alice("alice");
    const auto report = verify_store(store, alice);
    REQUIRE_FALSE(report.ok());
    CHECK(report.failure->seq == 3);
    CHECK(report.failure->reason.find("link") != std::string::npos);
}

TEST_CASE("an unreadable tail is a failure")
{
    auto records = good_records();
    records.pop_back(); // the store still claims sequence 3 but cannot serve it
    RiggedStore store(records, static_cast<Seq>(3));

    InsecureTestSigner alice("alice");
    const auto report = verify_store(store, alice);
    REQUIRE_FALSE(report.ok());
    CHECK(report.failure->reason.find("tail") != std::string::npos);
}
