#include "data/migrations.h"

#include <iterator>
#include <string>

namespace ninkasi::data {

namespace {

struct Migration {
    int version;
    const char* sql;
};

// Timestamps are stored as ISO-8601 UTC text, e.g. 2026-10-27T14:03:00Z.
constexpr Migration kMigrations[] = {
    {1, R"sql(
        -- Users exist from day one so every log row can record who did it.
        -- Until login arrives there is a single local user.
        CREATE TABLE users (
            id            INTEGER PRIMARY KEY,
            username      TEXT NOT NULL UNIQUE,
            display_name  TEXT NOT NULL,
            password_hash TEXT,                       -- NULL until login exists
            role          TEXT NOT NULL DEFAULT 'admin',
            created_at    TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ', 'now'))
        );
        INSERT INTO users (username, display_name) VALUES ('local', 'Local Brewer');

        -- Brewery-wide preferences (theme, units, ...) as key/value text.
        CREATE TABLE settings (
            key   TEXT PRIMARY KEY,
            value TEXT NOT NULL
        );

        CREATE TABLE audit_log (
            id        INTEGER PRIMARY KEY,
            at        TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ', 'now')),
            user_id   INTEGER NOT NULL REFERENCES users(id),
            action    TEXT NOT NULL,
            entity    TEXT,
            entity_id INTEGER,
            details   TEXT
        );
    )sql"},
    {2, R"sql(
        -- Equipment on the brewery floor. Positions and sizes are in floor
        -- units: one unit is one point at 100 % zoom.
        CREATE TABLE elements (
            id         INTEGER PRIMARY KEY,
            type       TEXT NOT NULL,
            name       TEXT NOT NULL,
            x          REAL NOT NULL,
            y          REAL NOT NULL,
            width      REAL NOT NULL,
            height     REAL NOT NULL,
            capacity_l REAL,                              -- vessels only
            created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ', 'now'))
        );

        -- Units an element shows instead of the brewery-wide choice.
        CREATE TABLE element_unit_overrides (
            element_id INTEGER NOT NULL REFERENCES elements(id) ON DELETE CASCADE,
            dimension  TEXT NOT NULL,
            unit       TEXT NOT NULL,
            PRIMARY KEY (element_id, dimension)
        );
    )sql"},
};

} // namespace

int latest_schema_version() {
    return std::end(kMigrations)[-1].version;
}

int schema_version(Database& db) {
    auto stmt = db.prepare("PRAGMA user_version;");
    stmt.step();
    return static_cast<int>(stmt.column_int64(0));
}

void migrate(Database& db) {
    const int current = schema_version(db);
    if (current > latest_schema_version()) {
        throw DbError("database schema version " + std::to_string(current) +
                      " is newer than this version of Ninkasi supports");
    }
    for (const auto& migration : kMigrations) {
        if (migration.version <= current) continue;
        Transaction tx(db);
        db.exec(migration.sql);
        db.exec("PRAGMA user_version = " + std::to_string(migration.version) + ";");
        tx.commit();
    }
}

} // namespace ninkasi::data
