#include "data/database.h"

#include <sqlite3.h>

namespace ninkasi::data {

namespace {

[[noreturn]] void throw_error(sqlite3* db, std::string_view context) {
    std::string message(context);
    message += ": ";
    message += db ? sqlite3_errmsg(db) : "unknown SQLite error";
    throw DbError(message);
}

} // namespace

// --- Statement --------------------------------------------------------------

Statement::Statement(sqlite3* db, std::string_view sql) : db_(db) {
    if (sqlite3_prepare_v2(db_, sql.data(), static_cast<int>(sql.size()), &stmt_, nullptr) != SQLITE_OK) {
        throw_error(db_, "prepare failed");
    }
}

Statement::~Statement() {
    sqlite3_finalize(stmt_);
}

Statement::Statement(Statement&& other) noexcept : db_(other.db_), stmt_(other.stmt_) {
    other.stmt_ = nullptr;
}

Statement& Statement::bind(int index, std::int64_t value) {
    if (sqlite3_bind_int64(stmt_, index, value) != SQLITE_OK) throw_error(db_, "bind failed");
    return *this;
}

Statement& Statement::bind(int index, double value) {
    if (sqlite3_bind_double(stmt_, index, value) != SQLITE_OK) throw_error(db_, "bind failed");
    return *this;
}

Statement& Statement::bind(int index, std::string_view value) {
    if (sqlite3_bind_text(stmt_, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT) != SQLITE_OK) {
        throw_error(db_, "bind failed");
    }
    return *this;
}

Statement& Statement::bind_blob(int index, std::span<const unsigned char> bytes) {
    // An empty blob is still a blob, not NULL.
    const int result = bytes.empty() ? sqlite3_bind_zeroblob(stmt_, index, 0)
                                     : sqlite3_bind_blob64(stmt_, index, bytes.data(), bytes.size(), SQLITE_TRANSIENT);
    if (result != SQLITE_OK) throw_error(db_, "bind failed");
    return *this;
}

Statement& Statement::bind_null(int index) {
    if (sqlite3_bind_null(stmt_, index) != SQLITE_OK) throw_error(db_, "bind failed");
    return *this;
}

Statement& Statement::bind_optional(int index, std::optional<double> value) {
    return value ? bind(index, *value) : bind_null(index);
}

Statement& Statement::bind_optional(int index, std::optional<std::int64_t> value) {
    return value ? bind(index, *value) : bind_null(index);
}

bool Statement::step() {
    switch (sqlite3_step(stmt_)) {
    case SQLITE_ROW:  return true;
    case SQLITE_DONE: return false;
    default:          throw_error(db_, "step failed");
    }
}

void Statement::execute() {
    while (step()) {
    }
}

void Statement::reset() {
    sqlite3_reset(stmt_);
}

std::int64_t Statement::column_int64(int col) const {
    return sqlite3_column_int64(stmt_, col);
}

double Statement::column_double(int col) const {
    return sqlite3_column_double(stmt_, col);
}

std::string Statement::column_text(int col) const {
    const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt_, col));
    return text ? std::string(text, sqlite3_column_bytes(stmt_, col)) : std::string();
}

std::vector<unsigned char> Statement::column_blob(int col) const {
    const auto* bytes = static_cast<const unsigned char*>(sqlite3_column_blob(stmt_, col));
    const int size = sqlite3_column_bytes(stmt_, col);
    return bytes ? std::vector<unsigned char>(bytes, bytes + size) : std::vector<unsigned char>();
}

bool Statement::column_is_null(int col) const {
    return sqlite3_column_type(stmt_, col) == SQLITE_NULL;
}

std::optional<double> Statement::column_optional_double(int col) const {
    if (column_is_null(col)) return std::nullopt;
    return column_double(col);
}

std::optional<std::int64_t> Statement::column_optional_int64(int col) const {
    if (column_is_null(col)) return std::nullopt;
    return column_int64(col);
}

// --- Database ---------------------------------------------------------------

Database::Database(const std::string& path) {
    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
        std::string message = "could not open database '" + path + "'";
        // sqlite3_open allocates a handle even on failure; it must be closed.
        DbError error(message + ": " + sqlite3_errmsg(db_));
        sqlite3_close(db_);
        throw error;
    }
    exec("PRAGMA foreign_keys = ON;");
    // WAL makes writes crash-safe and keeps reads fast; not supported in memory.
    if (path != ":memory:") exec("PRAGMA journal_mode = WAL;");
}

Database::~Database() {
    sqlite3_close(db_);
}

void Database::exec(std::string_view sql) {
    const std::string owned(sql); // sqlite3_exec needs a null-terminated string
    char* error = nullptr;
    if (sqlite3_exec(db_, owned.c_str(), nullptr, nullptr, &error) != SQLITE_OK) {
        std::string message = "exec failed: ";
        message += error ? error : "unknown error";
        sqlite3_free(error);
        throw DbError(message);
    }
}

Statement Database::prepare(std::string_view sql) {
    return Statement(db_, sql);
}

std::int64_t Database::last_insert_id() const {
    return sqlite3_last_insert_rowid(db_);
}

// --- Transaction ------------------------------------------------------------

Transaction::Transaction(Database& db) : db_(db) {
    db_.exec("BEGIN;");
}

Transaction::~Transaction() {
    if (!finished_) {
        try {
            db_.exec("ROLLBACK;");
        } catch (...) {
            // Destructors must not throw; a failed rollback leaves nothing to do.
        }
    }
}

void Transaction::commit() {
    db_.exec("COMMIT;");
    finished_ = true;
}

} // namespace ninkasi::data
