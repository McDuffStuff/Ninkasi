#pragma once
// Thin RAII wrapper around the SQLite C API.
//
// Every SQLite failure is turned into a DbError exception, so calling code
// can assume success and let errors propagate up to a single handler.

#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

namespace ninkasi::data {

class DbError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// A prepared SQL statement. Parameters and columns use SQLite's indexing:
// bind() is 1-based, column_*() is 0-based.
class Statement {
public:
    Statement(sqlite3* db, std::string_view sql);
    ~Statement();

    Statement(Statement&& other) noexcept;
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    Statement& operator=(Statement&&) = delete;

    Statement& bind(int index, std::int64_t value);
    Statement& bind(int index, int value) { return bind(index, static_cast<std::int64_t>(value)); }
    Statement& bind(int index, double value);
    Statement& bind(int index, std::string_view value);
    Statement& bind_blob(int index, std::span<const unsigned char> bytes);
    Statement& bind_null(int index);
    // Bind NULL when the value is empty.
    Statement& bind_optional(int index, std::optional<double> value);
    Statement& bind_optional(int index, std::optional<std::int64_t> value);

    // Advances to the next row. Returns true while a row is available.
    bool step();
    // Runs a statement that returns no rows (INSERT/UPDATE/DELETE).
    void execute();
    // Makes the statement reusable; bound values are kept.
    void reset();

    std::int64_t column_int64(int col) const;
    double column_double(int col) const;
    std::string column_text(int col) const;
    std::vector<unsigned char> column_blob(int col) const;
    bool column_is_null(int col) const;
    // nullopt for NULL.
    std::optional<double> column_optional_double(int col) const;
    std::optional<std::int64_t> column_optional_int64(int col) const;

private:
    sqlite3* db_;
    sqlite3_stmt* stmt_ = nullptr;
};

// An open database connection. Use ":memory:" as the path for tests.
class Database {
public:
    explicit Database(const std::string& path);
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    // Runs one or more SQL statements that return no rows.
    void exec(std::string_view sql);
    Statement prepare(std::string_view sql);
    std::int64_t last_insert_id() const;

private:
    sqlite3* db_ = nullptr;
};

// Scoped transaction: rolls back on destruction unless commit() was called.
// Every user action that writes more than one row should run inside one.
class Transaction {
public:
    explicit Transaction(Database& db);
    ~Transaction();

    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

    void commit();

private:
    Database& db_;
    bool finished_ = false;
};

} // namespace ninkasi::data
