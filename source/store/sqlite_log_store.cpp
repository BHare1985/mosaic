#include <mosaic/store/sqlite_log_store.h>

#include <mosaic/proto/codec.h>

#include <sqlite3.h>

namespace mosaic {
namespace {

    /// Small RAII wrapper: every sqlite handle needs finalising exactly once, even on an early return.
    struct Statement {
        sqlite3_stmt* handle = nullptr;
        Statement(sqlite3* db, const char* sql)
        {
            if (sqlite3_prepare_v2(db, sql, -1, &handle, nullptr) != SQLITE_OK)
                handle = nullptr;
        }
        ~Statement()
        {
            if (handle)
                sqlite3_finalize(handle);
        }
        Statement(const Statement&) = delete;
        Statement& operator=(const Statement&) = delete;

        bool ok() const { return handle != nullptr; }

        void bind_text(int index, const std::string& value)
        {
            sqlite3_bind_text(handle, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
        }
        void bind_int64(int index, std::int64_t value) { sqlite3_bind_int64(handle, index, value); }
        void bind_blob(int index, const Bytes& value)
        {
            sqlite3_bind_blob(handle, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
        }

        bool step_row() { return sqlite3_step(handle) == SQLITE_ROW; }
        bool step_done() { return sqlite3_step(handle) == SQLITE_DONE; }
    };

    /// A record is only "there" if it still decodes; a row that does not is a damaged tail, not a record.
    std::optional<Record> record_from(const std::uint8_t* data, int size)
    {
        if (!data || size <= 0)
            return std::nullopt;
        return decode_record(Bytes(data, data + size));
    }

} // namespace

struct SqliteLogStore::Impl {
    sqlite3* db = nullptr;
    std::string path;

    ~Impl()
    {
        if (db)
            sqlite3_close(db);
    }

    bool exec(const char* sql) { return sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK; }

    /// Last sequence whose stored record still decodes. Walks down past a damaged tail.
    std::optional<Seq> head_seq(const LogKey& key) const
    {
        Statement statement(db,
            "SELECT seq, blob FROM records WHERE writer = ?1 AND channel = ?2 ORDER BY seq DESC LIMIT 1");
        if (!statement.ok())
            return std::nullopt;
        statement.bind_text(1, key.writer);
        statement.bind_text(2, key.channel);
        if (!statement.step_row())
            return std::nullopt;

        Seq seq = static_cast<Seq>(sqlite3_column_int64(statement.handle, 0));
        if (record_from(static_cast<const std::uint8_t*>(sqlite3_column_blob(statement.handle, 1)),
                sqlite3_column_bytes(statement.handle, 1)))
            return seq;

        // Damaged tail: answer with the highest sequence that is still readable.
        while (seq > 0) {
            --seq;
            const auto record = get(key, seq);
            if (record)
                return seq;
        }
        return std::nullopt;
    }

    std::optional<Record> get(const LogKey& key, Seq seq) const
    {
        Statement statement(db, "SELECT blob FROM records WHERE writer = ?1 AND channel = ?2 AND seq = ?3");
        if (!statement.ok())
            return std::nullopt;
        statement.bind_text(1, key.writer);
        statement.bind_text(2, key.channel);
        statement.bind_int64(3, static_cast<std::int64_t>(seq));
        if (!statement.step_row())
            return std::nullopt;
        return record_from(static_cast<const std::uint8_t*>(sqlite3_column_blob(statement.handle, 0)),
            sqlite3_column_bytes(statement.handle, 0));
    }
};

SqliteLogStore::SqliteLogStore(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl))
{
}

std::optional<std::unique_ptr<SqliteLogStore>> SqliteLogStore::open(const std::string& path)
{
    auto impl = std::make_unique<Impl>();
    impl->path = path;
    if (sqlite3_open(path.c_str(), &impl->db) != SQLITE_OK) {
        if (impl->db) {
            sqlite3_close(impl->db);
            impl->db = nullptr;
        }
        return std::nullopt;
    }
    // WAL for concurrent readers, FULL so a successful commit is on disk before we report it.
    if (!impl->exec("PRAGMA journal_mode=WAL") || !impl->exec("PRAGMA synchronous=FULL")
        || !impl->exec("CREATE TABLE IF NOT EXISTS records ("
                       " writer TEXT NOT NULL, channel TEXT NOT NULL, seq INTEGER NOT NULL,"
                       " blob BLOB NOT NULL, PRIMARY KEY (writer, channel, seq)) WITHOUT ROWID")) {
        return std::nullopt;
    }
    return std::unique_ptr<SqliteLogStore>(new SqliteLogStore(std::move(impl)));
}

SqliteLogStore::~SqliteLogStore() = default;

AppendResult SqliteLogStore::append(const Record& record)
{
    const LogKey key = record.key();
    const auto head = impl_->head_seq(key);
    const Seq expected = head ? *head + 1 : 0;
    if (record.seq != expected)
        return AppendResult::NotNext;

    // One transaction per append: either the row is committed and durable, or nothing happened.
    if (!impl_->exec("BEGIN IMMEDIATE"))
        return AppendResult::NotNext;
    {
        Statement statement(impl_->db,
            "INSERT INTO records (writer, channel, seq, blob) VALUES (?1, ?2, ?3, ?4)");
        if (!statement.ok()) {
            impl_->exec("ROLLBACK");
            return AppendResult::NotNext;
        }
        statement.bind_text(1, key.writer);
        statement.bind_text(2, key.channel);
        statement.bind_int64(3, static_cast<std::int64_t>(record.seq));
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

std::optional<Seq> SqliteLogStore::head_seq(const LogKey& key) const { return impl_->head_seq(key); }

std::optional<Record> SqliteLogStore::get(const LogKey& key, Seq seq) const { return impl_->get(key, seq); }

std::vector<Record> SqliteLogStore::read(const LogKey& key, Seq from, std::size_t count) const
{
    std::vector<Record> records;
    if (count == 0)
        return records;
    Statement statement(impl_->db,
        "SELECT blob FROM records WHERE writer = ?1 AND channel = ?2 AND seq >= ?3 ORDER BY seq LIMIT ?4");
    if (!statement.ok())
        return records;
    statement.bind_text(1, key.writer);
    statement.bind_text(2, key.channel);
    statement.bind_int64(3, static_cast<std::int64_t>(from));
    statement.bind_int64(4, static_cast<std::int64_t>(count));
    while (statement.step_row()) {
        const auto record = record_from(static_cast<const std::uint8_t*>(sqlite3_column_blob(statement.handle, 0)),
            sqlite3_column_bytes(statement.handle, 0));
        if (!record)
            break; // damaged tail: everything after it is untrustworthy, so stop here
        records.push_back(*record);
    }
    return records;
}

std::vector<LogKey> SqliteLogStore::keys() const
{
    std::vector<LogKey> out;
    Statement statement(impl_->db, "SELECT DISTINCT writer, channel FROM records ORDER BY writer, channel");
    if (!statement.ok())
        return out;
    while (statement.step_row()) {
        const auto* writer = reinterpret_cast<const char*>(sqlite3_column_text(statement.handle, 0));
        const auto* channel = reinterpret_cast<const char*>(sqlite3_column_text(statement.handle, 1));
        out.push_back(LogKey { writer ? writer : "", channel ? channel : "" });
    }
    return out;
}

} // namespace mosaic
