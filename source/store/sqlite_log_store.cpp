#include <mosaic/proto/codec.h>
#include <mosaic/store/sqlite_log_store.h>

#include <map>
#include <sqlite3.h>

using std::int64_t;
using std::make_unique;
using std::map;
using std::move;
using std::nullopt;
using std::optional;
using std::size_t;
using std::string;
using std::uint8_t;
using std::unique_ptr;
using std::vector;

namespace mosaic {
namespace {

/// Small RAII wrapper: every sqlite handle needs finalising exactly once, even on an early return.
struct Statement {
    sqlite3_stmt* handle = nullptr;
    Statement(sqlite3* db, const char* sql) {
        if (sqlite3_prepare_v2(db, sql, -1, &handle, nullptr) != SQLITE_OK)
            handle = nullptr;
    }
    ~Statement() {
        if (handle)
            sqlite3_finalize(handle);
    }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    bool ok() const {
        return handle != nullptr;
    }

    void bind_text(int index, const string& value) {
        sqlite3_bind_text(handle, index, value.data(), static_cast<int>(value.size()),
                          SQLITE_TRANSIENT);
    }
    void bind_int64(int index, int64_t value) {
        sqlite3_bind_int64(handle, index, value);
    }
    void bind_blob(int index, const Bytes& value) {
        sqlite3_bind_blob(handle, index, value.data(), static_cast<int>(value.size()),
                          SQLITE_TRANSIENT);
    }

    bool step_row() {
        return sqlite3_step(handle) == SQLITE_ROW;
    }
    bool step_done() {
        return sqlite3_step(handle) == SQLITE_DONE;
    }
};

/// A record is only "there" if it still decodes; a row that does not is a damaged tail, not a
/// record.
optional<Record> record_from(const uint8_t* data, int size) {
    if (!data || size <= 0)
        return nullopt;
    return decode_record(Bytes(data, data + size));
}

}  // namespace

struct SqliteLogStore::Impl {
    sqlite3* db = nullptr;
    string path;

    ~Impl() {
        if (db)
            sqlite3_close(db);
    }

    bool exec(const char* sql) {
        return sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
    }

    /// Last sequence whose stored record still decodes. Walks down past a damaged tail.
    optional<Seq> head_seq(const LogKey& key) const {
        Statement statement(db, "SELECT seq, blob FROM records WHERE writer = ?1 AND channel = ?2 "
                                "ORDER BY seq DESC LIMIT 1");
        if (!statement.ok())
            return nullopt;
        statement.bind_text(1, key.writer);
        statement.bind_text(2, key.channel);
        if (!statement.step_row())
            return nullopt;

        Seq seq = static_cast<Seq>(sqlite3_column_int64(statement.handle, 0));
        if (record_from(static_cast<const uint8_t*>(sqlite3_column_blob(statement.handle, 1)),
                        sqlite3_column_bytes(statement.handle, 1)))
            return seq;

        // Damaged tail: answer with the highest sequence that is still readable.
        while (seq > 0) {
            --seq;
            const auto record = get(key, seq);
            if (record)
                return seq;
        }
        return nullopt;
    }

    optional<Record> get(const LogKey& key, Seq seq) const {
        Statement statement(
            db, "SELECT blob FROM records WHERE writer = ?1 AND channel = ?2 AND seq = ?3");
        if (!statement.ok())
            return nullopt;
        statement.bind_text(1, key.writer);
        statement.bind_text(2, key.channel);
        statement.bind_int64(3, static_cast<int64_t>(seq));
        if (!statement.step_row())
            return nullopt;
        return record_from(static_cast<const uint8_t*>(sqlite3_column_blob(statement.handle, 0)),
                           sqlite3_column_bytes(statement.handle, 0));
    }
};

SqliteLogStore::SqliteLogStore(unique_ptr<Impl> impl, Durability durability)
    : impl_(move(impl)), durability_(durability) {}

optional<unique_ptr<SqliteLogStore>> SqliteLogStore::open(const string& path,
                                                          Durability durability) {
    auto impl = make_unique<Impl>();
    impl->path = path;
    if (sqlite3_open(path.c_str(), &impl->db) != SQLITE_OK) {
        if (impl->db) {
            sqlite3_close(impl->db);
            impl->db = nullptr;
        }
        return nullopt;
    }
    // WAL for concurrent readers, FULL so a successful commit is on disk before we report it.
    const char* sync =
        durability == Durability::Batched ? "PRAGMA synchronous=NORMAL" : "PRAGMA synchronous=FULL";
    if (!impl->exec("PRAGMA journal_mode=WAL") || !impl->exec(sync) ||
        !impl->exec("CREATE TABLE IF NOT EXISTS records ("
                    " writer TEXT NOT NULL, channel TEXT NOT NULL, seq INTEGER NOT NULL,"
                    " blob BLOB NOT NULL, PRIMARY KEY (writer, channel, seq)) WITHOUT ROWID")
        // Evidence needs its own table: a log store holds one record per (writer, channel, seq),
        // and a fork is exactly the case where two exist.
        || !impl->exec("CREATE TABLE IF NOT EXISTS forks ("
                       " writer TEXT NOT NULL, channel TEXT NOT NULL, seq INTEGER NOT NULL,"
                       " accepted BLOB NOT NULL, conflicting BLOB NOT NULL,"
                       " PRIMARY KEY (writer, channel, seq)) WITHOUT ROWID")
        // A checkpoint outlives the rows it covers, so it cannot live in `records`.
        || !impl->exec("CREATE TABLE IF NOT EXISTS checkpoints ("
                       " writer TEXT NOT NULL, channel TEXT NOT NULL, blob BLOB NOT NULL,"
                       " PRIMARY KEY (writer, channel)) WITHOUT ROWID")) {
        return nullopt;
    }
    return unique_ptr<SqliteLogStore>(new SqliteLogStore(move(impl), durability));
}

SqliteLogStore::~SqliteLogStore() = default;

bool SqliteLogStore::put_checkpoint(const LogKey& key, const Bytes& blob) {
    Statement statement(impl_->db, "INSERT OR REPLACE INTO checkpoints (writer, channel, blob)"
                                   " VALUES (?, ?, ?)");
    if (!statement.ok())
        return false;
    statement.bind_text(1, key.writer);
    statement.bind_text(2, key.channel);
    statement.bind_blob(3, blob);
    return statement.step_done();
}

std::optional<Bytes> SqliteLogStore::checkpoint(const LogKey& key) const {
    Statement statement(impl_->db, "SELECT blob FROM checkpoints WHERE writer = ? AND channel = ?");
    if (!statement.ok())
        return std::nullopt;
    statement.bind_text(1, key.writer);
    statement.bind_text(2, key.channel);
    if (sqlite3_step(statement.handle) != SQLITE_ROW)
        return std::nullopt;
    const auto* data = static_cast<const uint8_t*>(sqlite3_column_blob(statement.handle, 0));
    const auto size = sqlite3_column_bytes(statement.handle, 0);
    if (data == nullptr || size <= 0)
        return std::nullopt;
    return Bytes(data, data + size);
}

std::optional<std::size_t> SqliteLogStore::truncate_before(const LogKey& key, Seq first_kept) {
    Statement statement(impl_->db,
                        "DELETE FROM records WHERE writer = ? AND channel = ? AND seq < ?");
    if (!statement.ok())
        return std::nullopt;
    statement.bind_text(1, key.writer);
    statement.bind_text(2, key.channel);
    statement.bind_int64(3, static_cast<int64_t>(first_kept));
    if (!statement.step_done())
        return std::nullopt;
    return static_cast<std::size_t>(sqlite3_changes(impl_->db));
}

AppendResult SqliteLogStore::append_batch(const vector<Record>& records) {
    if (records.empty())
        return AppendResult::Ok;
    if (!impl_->exec("BEGIN IMMEDIATE"))
        return AppendResult::NotNext;

    // Every record must be next in its own log, in order - the same rule as appending one at a
    // time. Any failure rolls the whole batch back, so a batch never half-applies.
    Statement statement(impl_->db,
                        "INSERT INTO records (writer, channel, seq, blob) VALUES (?1, ?2, ?3, ?4)");
    if (!statement.ok()) {
        impl_->exec("ROLLBACK");
        return AppendResult::NotNext;
    }
    map<LogKey, Seq> expected;  // what each log should be at, advanced as the batch goes
    for (const auto& record : records) {
        const LogKey key = record.key();
        auto it = expected.find(key);
        if (it == expected.end()) {
            const auto head = impl_->head_seq(key);
            it = expected.emplace(key, head ? *head + 1 : 0).first;
        }
        if (record.seq != it->second) {
            impl_->exec("ROLLBACK");
            return AppendResult::NotNext;
        }
        it->second = it->second + 1;

        sqlite3_reset(statement.handle);
        statement.bind_text(1, key.writer);
        statement.bind_text(2, key.channel);
        statement.bind_int64(3, static_cast<int64_t>(record.seq));
        statement.bind_blob(4, encode_record(record));
        if (!statement.step_done()) {
            impl_->exec("ROLLBACK");
            return AppendResult::NotNext;
        }
    }
    if (!impl_->exec("COMMIT")) {
        impl_->exec("ROLLBACK");
        return AppendResult::NotNext;
    }
    return AppendResult::Ok;
}

AppendResult SqliteLogStore::append(const Record& record) {
    const LogKey key = record.key();
    const auto head = impl_->head_seq(key);
    const Seq expected = head ? *head + 1 : 0;
    if (record.seq != expected)
        return AppendResult::NotNext;

    // One transaction per append: either the row is committed and durable, or nothing happened.
    if (!impl_->exec("BEGIN IMMEDIATE"))
        return AppendResult::NotNext;
    {
        Statement statement(
            impl_->db, "INSERT INTO records (writer, channel, seq, blob) VALUES (?1, ?2, ?3, ?4)");
        if (!statement.ok()) {
            impl_->exec("ROLLBACK");
            return AppendResult::NotNext;
        }
        statement.bind_text(1, key.writer);
        statement.bind_text(2, key.channel);
        statement.bind_int64(3, static_cast<int64_t>(record.seq));
        statement.bind_blob(4, encode_record(record));
        if (!statement.step_done()) {
            impl_->exec("ROLLBACK");
            return AppendResult::NotNext;
        }
    }
    if (!impl_->exec("COMMIT")) {
        impl_->exec("ROLLBACK");
        return AppendResult::NotNext;
    }
    return AppendResult::Ok;
}

optional<Seq> SqliteLogStore::head_seq(const LogKey& key) const {
    return impl_->head_seq(key);
}

optional<Record> SqliteLogStore::get(const LogKey& key, Seq seq) const {
    return impl_->get(key, seq);
}

vector<Record> SqliteLogStore::read(const LogKey& key, Seq from, size_t count) const {
    vector<Record> records;
    if (count == 0)
        return records;
    Statement statement(impl_->db, "SELECT blob FROM records WHERE writer = ?1 AND channel = ?2 "
                                   "AND seq >= ?3 ORDER BY seq LIMIT ?4");
    if (!statement.ok())
        return records;
    statement.bind_text(1, key.writer);
    statement.bind_text(2, key.channel);
    statement.bind_int64(3, static_cast<int64_t>(from));
    statement.bind_int64(4, static_cast<int64_t>(count));
    while (statement.step_row()) {
        const auto record =
            record_from(static_cast<const uint8_t*>(sqlite3_column_blob(statement.handle, 0)),
                        sqlite3_column_bytes(statement.handle, 0));
        if (!record)
            break;  // damaged tail: everything after it is untrustworthy, so stop here
        records.push_back(*record);
    }
    return records;
}

bool SqliteLogStore::save_fork(const ForkEvidence& evidence) {
    Statement statement(impl_->db, "INSERT OR REPLACE INTO forks (writer, channel, seq, accepted, "
                                   "conflicting) VALUES (?1, ?2, ?3, ?4, ?5)");
    if (!statement.ok())
        return false;
    statement.bind_text(1, evidence.key.writer);
    statement.bind_text(2, evidence.key.channel);
    statement.bind_int64(3, static_cast<int64_t>(evidence.seq));
    statement.bind_blob(4, encode_record(evidence.accepted));
    statement.bind_blob(5, encode_record(evidence.conflicting));
    return statement.step_done();
}

vector<ForkEvidence> SqliteLogStore::load_forks(const LogKey& key) const {
    vector<ForkEvidence> out;
    Statement statement(impl_->db, "SELECT seq, accepted, conflicting FROM forks WHERE writer = ?1 "
                                   "AND channel = ?2 ORDER BY seq");
    if (!statement.ok())
        return out;
    statement.bind_text(1, key.writer);
    statement.bind_text(2, key.channel);
    while (statement.step_row()) {
        const auto accepted =
            record_from(static_cast<const uint8_t*>(sqlite3_column_blob(statement.handle, 1)),
                        sqlite3_column_bytes(statement.handle, 1));
        const auto conflicting =
            record_from(static_cast<const uint8_t*>(sqlite3_column_blob(statement.handle, 2)),
                        sqlite3_column_bytes(statement.handle, 2));
        if (!accepted || !conflicting)
            continue;  // unreadable evidence is skipped, never half-returned
        out.push_back(ForkEvidence{key, static_cast<Seq>(sqlite3_column_int64(statement.handle, 0)),
                                   *accepted, *conflicting});
    }
    return out;
}

vector<LogKey> SqliteLogStore::keys() const {
    vector<LogKey> out;
    Statement statement(impl_->db,
                        "SELECT DISTINCT writer, channel FROM records ORDER BY writer, channel");
    if (!statement.ok())
        return out;
    while (statement.step_row()) {
        const auto* writer =
            reinterpret_cast<const char*>(sqlite3_column_text(statement.handle, 0));
        const auto* channel =
            reinterpret_cast<const char*>(sqlite3_column_text(statement.handle, 1));
        out.push_back(LogKey{writer ? writer : "", channel ? channel : ""});
    }
    return out;
}

}  // namespace mosaic
