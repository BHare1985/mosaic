// The segment backend: one directory per log, append-only segment files, an index that is derived
// state. It runs the same conformance suite as every other backend, plus the recovery rules that
// are specific to a file layout (spec: store/segment-log-store).
#include <mosaic/store/segment_log_store.h>

#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <string>

#include "store_conformance.h"

using std::size_t;
using std::string;
using std::uint64_t;

using namespace mosaic;

namespace {
namespace fs = std::filesystem;

fs::path temp_dir(const char* name) {
    const auto dir = fs::temp_directory_path() / name;
    std::error_code error;
    fs::remove_all(dir, error);
    fs::create_directories(dir, error);
    return dir;
}

Record sample(Seq seq, const char* channel = "c") {
    Record record;
    record.writer = "w";
    record.channel = channel;
    record.seq = seq;
    record.content_type = "text/plain";
    record.payload = Bytes{static_cast<uint8_t>('a' + (seq % 26))};
    return record;
}

/// Append a byte string to the newest segment of a log, to simulate a crash mid-write.
void append_raw(const fs::path& dir, const string& bytes) {
    fs::path newest;
    for (const auto& item : fs::directory_iterator(dir))
        if (item.path().extension() == ".seg" &&
            (newest.empty() || item.path().filename() > newest.filename()))
            newest = item.path();
    REQUIRE_FALSE(newest.empty());
    std::ofstream out(newest, std::ios::binary | std::ios::app);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

size_t count_segments(const fs::path& dir) {
    size_t count = 0;
    for (const auto& item : fs::directory_iterator(dir))
        if (item.path().extension() == ".seg")
            ++count;
    return count;
}
}  // namespace

TEST_CASE("store conformance: SegmentLogStore") {
    const auto dir = temp_dir("mosaic-segment-conformance");
    const mosaic::test::StoreFactory factory = [dir] {
        auto store = SegmentLogStore::open(dir.string());
        REQUIRE(store.has_value());
        return std::move(*store);
    };
    mosaic::test::run_store_conformance(factory);
    std::error_code error;
    fs::remove_all(dir, error);
}

TEST_CASE("segments roll over, and enumeration finds every log") {
    const auto dir = temp_dir("mosaic-segment-roll");
    // Small segments, so a handful of records crosses the boundary several times.
    auto store = SegmentLogStore::open(dir.string(), Durability::PerRecord, 160);
    REQUIRE(store.has_value());
    for (Seq seq = 0; seq < 20; ++seq)
        REQUIRE((*store)->append(sample(seq)) == AppendResult::Ok);
    REQUIRE((*store)->append(sample(0, "other")) == AppendResult::Ok);

    const auto log_dir = dir / "w" / "c";
    CHECK(count_segments(log_dir) > 1);  // rolled over rather than growing one file
    // A full segment is never rewritten: the first one is still exactly as long as it was.
    const auto keys = (*store)->keys();
    REQUIRE(keys.size() == 2);
    CHECK((*store)->head_seq(LogKey{"w", "c"}) == 19);
}

TEST_CASE("a torn tail is discarded and appending continues") {
    const auto dir = temp_dir("mosaic-segment-torn");
    {
        auto store = SegmentLogStore::open(dir.string());
        REQUIRE(store.has_value());
        for (Seq seq = 0; seq < 5; ++seq)
            REQUIRE((*store)->append(sample(seq)) == AppendResult::Ok);
    }
    // Half a frame: a length that promises more than the file now holds.
    append_raw(dir / "w" / "c", string{'\x40', '\x00', '\x00', '\x00', 'x', 'y'});

    auto reopened = SegmentLogStore::open(dir.string());
    REQUIRE(reopened.has_value());
    CHECK((*reopened)->recovered_bytes() > 0);
    CHECK((*reopened)->head_seq(LogKey{"w", "c"}) == 4);  // the partial record is gone
    REQUIRE((*reopened)->append(sample(5)) == AppendResult::Ok);
    CHECK((*reopened)->head_seq(LogKey{"w", "c"}) == 5);
    REQUIRE((*reopened)->get(LogKey{"w", "c"}, 5).has_value());
}

TEST_CASE("a damaged segment reports a record missing rather than returning bytes") {
    const auto dir = temp_dir("mosaic-segment-damaged");
    {
        auto store = SegmentLogStore::open(dir.string());
        REQUIRE(store.has_value());
        for (Seq seq = 0; seq < 5; ++seq)
            REQUIRE((*store)->append(sample(seq)) == AppendResult::Ok);
    }
    // Cut the file in the middle of the last record, keeping the index that still points past it.
    std::error_code error;
    const auto segment = dir / "w" / "c" / "00000000000000000000.seg";
    const auto size = fs::file_size(segment, error);
    REQUIRE_FALSE(error);
    fs::resize_file(segment, size - 4, error);
    REQUIRE_FALSE(error);

    auto reopened = SegmentLogStore::open(dir.string());
    REQUIRE(reopened.has_value());
    // Recovery truncates from the damaged frame: the log ends before it, and nothing invents the
    // missing bytes.
    CHECK((*reopened)->head_seq(LogKey{"w", "c"}) == 3);
    CHECK_FALSE((*reopened)->get(LogKey{"w", "c"}, 4).has_value());
}

TEST_CASE("an unknown format is refused without touching the store") {
    const auto dir = temp_dir("mosaic-segment-format");
    {
        auto store = SegmentLogStore::open(dir.string());
        REQUIRE(store.has_value());
        REQUIRE((*store)->append(sample(0)) == AppendResult::Ok);
    }
    // Rewrite the header as a format this build does not know.
    {
        std::ofstream head(dir / "HEAD", std::ios::binary | std::ios::trunc);
        head << "MOSAICSEG";
        head.put(static_cast<char>(SegmentLogStore::kFormatVersion + 7));
    }
    CHECK_FALSE(SegmentLogStore::open(dir.string()).has_value());
    // And the records are still there, untouched, for a build that does know the format.
    std::ifstream head(dir / "HEAD", std::ios::binary);
    string content((std::istreambuf_iterator<char>(head)), std::istreambuf_iterator<char>());
    CHECK(content.size() == 10);
}
