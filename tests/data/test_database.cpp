#include "app/session.h"
#include "data/database.h"
#include "data/migrations.h"

#include <doctest/doctest.h>

#include <stdexcept>

using namespace ninkasi;

TEST_CASE("migrate brings a fresh database to the latest schema") {
    data::Database db(":memory:");
    CHECK(data::schema_version(db) == 0);

    data::migrate(db);
    CHECK(data::schema_version(db) == data::latest_schema_version());
}

TEST_CASE("migrate is safe to run more than once") {
    data::Database db(":memory:");
    data::migrate(db);
    data::migrate(db);
    CHECK(data::schema_version(db) == data::latest_schema_version());

    auto stmt = db.prepare("SELECT COUNT(*) FROM users;");
    REQUIRE(stmt.step());
    CHECK(stmt.column_int64(0) == 1);
}

TEST_CASE("migrate refuses a database from a newer version of Ninkasi") {
    data::Database db(":memory:");
    db.exec("PRAGMA user_version = 9999;");
    CHECK_THROWS_AS(data::migrate(db), data::DbError);
}

TEST_CASE("the local user is seeded and opens a session") {
    data::Database db(":memory:");
    data::migrate(db);

    const app::Session session = app::open_local_session(db);
    CHECK(session.user_id > 0);
    CHECK(session.display_name == "Local Brewer");
}

TEST_CASE("Transaction rolls back unless committed") {
    data::Database db(":memory:");
    db.exec("CREATE TABLE t (x INTEGER);");

    auto count = [&] {
        auto stmt = db.prepare("SELECT COUNT(*) FROM t;");
        stmt.step();
        return stmt.column_int64(0);
    };

    SUBCASE("an exception mid-transaction leaves no partial writes") {
        try {
            data::Transaction tx(db);
            db.exec("INSERT INTO t VALUES (1);");
            throw std::runtime_error("simulated failure");
        } catch (const std::runtime_error&) {
        }
        CHECK(count() == 0);
    }

    SUBCASE("commit keeps the writes") {
        data::Transaction tx(db);
        db.exec("INSERT INTO t VALUES (1);");
        tx.commit();
        CHECK(count() == 1);
    }
}

TEST_CASE("Statement binds and reads back values") {
    data::Database db(":memory:");
    db.exec("CREATE TABLE t (i INTEGER, d REAL, s TEXT, n TEXT);");

    auto insert = db.prepare("INSERT INTO t VALUES (?, ?, ?, ?);");
    insert.bind(1, 42).bind(2, 1.048).bind(3, "Pale Ale").bind_null(4);
    insert.execute();

    auto select = db.prepare("SELECT i, d, s, n FROM t;");
    REQUIRE(select.step());
    CHECK(select.column_int64(0) == 42);
    CHECK(select.column_double(1) == doctest::Approx(1.048));
    CHECK(select.column_text(2) == "Pale Ale");
    CHECK(select.column_is_null(3));
    CHECK_FALSE(select.step());
}

TEST_CASE("bad SQL raises DbError") {
    data::Database db(":memory:");
    CHECK_THROWS_AS(db.exec("NOT VALID SQL;"), data::DbError);
    CHECK_THROWS_AS(db.prepare("SELECT * FROM missing_table;"), data::DbError);
}
