// STORE-1: the durable backend. Everything here is about the promise "reported success means
// committed", and about surviving the thing that actually happens in production - a torn tail.
#include "store_conformance.h"

#include <chrono>
#include <cstdio>
#include <doctest/doctest.h>
#include <mosaic/store/memory_log_store.h>
#include <mosaic/store/sqlite_log_store.h>
#include <string>

#ifdef MOSAIC_HAS_SQLITE
#include <sqlite3.h>
#endif

using namespace mosaic;

namespace {
std::string temp_path(const char* name)
{
    std::remove(name);
    return name;
}

Record record_at(Seq seq)
{
    Record record;
    record.writer = "w";
    record.channel = "c";
    record.seq = seq;
    record.content_type = "text/plain";
    record.payload = Bytes { static_cast<std::uint8_t>('a' + (seq % 26)) };
    return record;
}
} // namespace

#ifdef MOSAIC_HAS_SQLITE

TEST_CASE("store conformance: SqliteLogStore")
{
    const std::string path = temp_path("test-store-conformance.db");
    const mosaic::test::StoreFactory factory = [path] {
        auto store = SqliteLogStore::open(path);
        REQUIRE(store.has_value()); // a fresh store per case, same file: reopen is part of the contract
        return std::move(*store);
    };
    mosaic::test::run_store_conformance(factory);
    std::remove(path.c_str());
    std::remove((path + "-wal").c_str());
    std::remove((path + "-shm").c_str());
}

TEST_CASE("a successful append is on disk before the caller sees it")
{
    const std::string path = temp_path("test-store-durable.db");
    {
        auto store = SqliteLogStore::open(path);
        REQUIRE(store.has_value());
        for (Seq seq = 0; seq < 5; ++seq)
            REQUIRE((*store)->append(record_at(seq)) == AppendResult::Ok);
    } // closed, standing in for a crash right after the last success

    auto reopened = SqliteLogStore::open(path);
    REQUIRE(reopened.has_value());
    CHECK((*reopened)->head_seq({ "w", "c" }) == 4);
    CHECK((*reopened)->get({ "w", "c" }, 4).has_value());
    // And the log continues rather than restarting.
    CHECK((*reopened)->append(record_at(5)) == AppendResult::Ok);
    CHECK((*reopened)->head_seq({ "w", "c" }) == 5);
    std::remove(path.c_str());
    std::remove((path + "-wal").c_str());
    std::remove((path + "-shm").c_str());
}

TEST_CASE("a damaged tail ends the log instead of breaking it")
{
    const std::string path = temp_path("test-store-torn.db");
    {
        auto store = SqliteLogStore::open(path);
        REQUIRE(store.has_value());
        for (Seq seq = 0; seq < 4; ++seq)
            REQUIRE((*store)->append(record_at(seq)) == AppendResult::Ok);
    }

    // Corrupt the last stored record at the byte level: exactly what a torn write looks like.
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(path.c_str(), &db) == SQLITE_OK);
    REQUIRE(sqlite3_exec(db, "UPDATE records SET blob = x'00' WHERE seq = 3", nullptr, nullptr, nullptr)
        == SQLITE_OK);
    sqlite3_close(db);

    auto reopened = SqliteLogStore::open(path);
    REQUIRE(reopened.has_value());
    CHECK((*reopened)->head_seq({ "w", "c" }) == 2); // last complete record, not a failure to open
    const auto readable = (*reopened)->read({ "w", "c" }, 0, 10);
    REQUIRE(readable.size() == 3); // stops at the damage
    for (const auto& record : readable)
        CHECK(record.seq < 3);
    CHECK_FALSE((*reopened)->get({ "w", "c" }, 3).has_value());
    std::remove(path.c_str());
    std::remove((path + "-wal").c_str());
    std::remove((path + "-shm").c_str());
}

TEST_CASE("append throughput is measured, not guessed")
{
    const std::string path = temp_path("test-store-bench.db");
    auto store = SqliteLogStore::open(path);
    REQUIRE(store.has_value());

    const auto start = std::chrono::steady_clock::now();
    constexpr Seq total = 5000;
    for (Seq seq = 0; seq < total; ++seq)
        REQUIRE((*store)->append(record_at(seq)) == AppendResult::Ok);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
    MESSAGE("SqliteLogStore: " << total << " durable appends in " << micros << "us ("
                               << (micros / total) << "us each)");
    std::remove(path.c_str());
    std::remove((path + "-wal").c_str());
    std::remove((path + "-shm").c_str());
}

#else

TEST_CASE("the SQLite store is not built in this configuration")
{
    CHECK(true); // MOSAIC_WITH_SQLITE=OFF: nothing durable to verify
}

#endif // MOSAIC_HAS_SQLITE
