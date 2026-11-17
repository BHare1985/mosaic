#include "store_conformance.h"

#include <algorithm>
#include <doctest/doctest.h>
#include <mosaic/store/memory_log_store.h>

using namespace mosaic;
using mosaic::test::StoreFactory;

namespace {
Record make_record(const WriterId& writer, const Channel& channel, Seq seq)
{
    Record record;
    record.writer = writer;
    record.channel = channel;
    record.seq = seq;
    record.content_type = "text/plain";
    record.payload = Bytes { static_cast<std::uint8_t>('a' + (seq % 26)) };
    return record;
}
} // namespace

void mosaic::test::run_store_conformance(const StoreFactory& factory)
{
    SUBCASE("an empty store has no logs and no head")
    {
        auto store = factory();
        REQUIRE(store);
        CHECK(store->keys().empty());
        CHECK_FALSE(store->head_seq({ "w", "c" }).has_value());
        CHECK_FALSE(store->get({ "w", "c" }, 0).has_value());
        CHECK(store->read({ "w", "c" }, 0, 10).empty());
    }

    SUBCASE("appends are contiguous and never rewrite")
    {
        auto store = factory();
        REQUIRE(store);

        CHECK(store->append(make_record("w", "c", 0)) == AppendResult::Ok);
        CHECK(store->head_seq({ "w", "c" }) == 0);

        // Same sequence again: refused, and the stored record is untouched.
        auto second = make_record("w", "c", 0);
        second.payload = Bytes { 'z' };
        CHECK(store->append(second) == AppendResult::NotNext);
        const auto kept = store->get({ "w", "c" }, 0);
        REQUIRE(kept.has_value());
        CHECK(kept->payload != second.payload);

        // A gap is refused just as firmly as a repeat.
        CHECK(store->append(make_record("w", "c", 5)) == AppendResult::NotNext);
        CHECK(store->head_seq({ "w", "c" }) == 0);

        CHECK(store->append(make_record("w", "c", 1)) == AppendResult::Ok);
        CHECK(store->head_seq({ "w", "c" }) == 1);
    }

    SUBCASE("reads are ordered, bounded and never invent data")
    {
        auto store = factory();
        REQUIRE(store);
        for (Seq seq = 0; seq < 6; ++seq)
            REQUIRE(store->append(make_record("w", "c", seq)) == AppendResult::Ok);

        const auto window = store->read({ "w", "c" }, 2, 3);
        REQUIRE(window.size() == 3);
        CHECK(window[0].seq == 2);
        CHECK(window[1].seq == 3);
        CHECK(window[2].seq == 4);

        CHECK(store->read({ "w", "c" }, 5, 10).size() == 1); // clipped at the head
        CHECK(store->read({ "w", "c" }, 6, 10).empty()); // past the head
        CHECK(store->read({ "w", "c" }, 2, 0).empty()); // zero count
    }

    SUBCASE("logs are separate per writer and per channel")
    {
        auto store = factory();
        REQUIRE(store);
        for (Seq seq = 0; seq < 3; ++seq) {
            REQUIRE(store->append(make_record("w1", "c", seq)) == AppendResult::Ok);
            REQUIRE(store->append(make_record("w2", "c", seq)) == AppendResult::Ok);
            REQUIRE(store->append(make_record("w1", "other", seq)) == AppendResult::Ok);
        }
        CHECK(store->head_seq({ "w1", "c" }) == 2);
        CHECK(store->head_seq({ "w2", "c" }) == 2);
        CHECK(store->head_seq({ "w1", "other" }) == 2);

        // Reading one log must not leak records from another with the same sequence numbers.
        const auto only_w1 = store->read({ "w1", "c" }, 0, 100);
        REQUIRE(only_w1.size() == 3);
        for (const auto& record : only_w1) {
            CHECK(record.writer == "w1");
            CHECK(record.channel == "c");
        }
    }

    SUBCASE("enumeration lists exactly the logs held")
    {
        auto store = factory();
        REQUIRE(store);
        REQUIRE(store->append(make_record("w1", "a", 0)) == AppendResult::Ok);
        REQUIRE(store->append(make_record("w2", "b", 0)) == AppendResult::Ok);
        REQUIRE(store->append(make_record("w3", "c", 0)) == AppendResult::Ok);

        const auto keys = store->keys();
        REQUIRE(keys.size() == 3);
        CHECK(std::is_sorted(keys.begin(), keys.end()));
    }

    SUBCASE("a batch behaves exactly like the same records appended one at a time")
    {
        auto store = factory();
        REQUIRE(store);

        std::vector<Record> batch;
        for (Seq seq = 0; seq < 3; ++seq)
            batch.push_back(make_record("w", "c", seq));
        CHECK(store->append_batch(batch) == AppendResult::Ok);
        CHECK(store->head_seq({ "w", "c" }) == 2);
        CHECK(store->read({ "w", "c" }, 0, 10).size() == 3);

        // A batch with a gap in the middle is refused whole: a batch never half-applies.
        std::vector<Record> gapped { make_record("w", "c", 4), make_record("w", "c", 3) };
        CHECK(store->append_batch(gapped) == AppendResult::NotNext);
        CHECK(store->head_seq({ "w", "c" }) == 2);
        CHECK_FALSE(store->get({ "w", "c" }, 4).has_value());
    }

    SUBCASE("the store is dumb: it accepts a record it would never verify")
    {
        auto store = factory();
        REQUIRE(store);
        auto forged = make_record("w", "c", 0);
        forged.sig = Bytes { 1, 2, 3 }; // nonsense signature
        CHECK(store->append(forged) == AppendResult::Ok); // rejection belongs to ReplicaLog, not here
        CHECK(store->head_seq({ "w", "c" }) == 0);
    }
}

TEST_CASE("store conformance: MemoryLogStore")
{
    const StoreFactory factory = [] { return std::make_unique<MemoryLogStore>(); };
    mosaic::test::run_store_conformance(factory);
}
