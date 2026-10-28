#include "app/backup_service.h"
#include "app/floor_service.h"
#include "app/session.h"
#include "data/database.h"
#include "data/migrations.h"
#include "data/settings_repository.h"
#include "test_helpers.h"

#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>
#include <string>

using namespace ninkasi;
namespace fs = std::filesystem;

namespace {

// A folder of its own for the test, removed afterwards.
struct TempFolder {
    fs::path path = fs::temp_directory_path() / ("ninkasi-test-" + std::to_string(std::random_device{}()));
    TempFolder() { fs::create_directories(path); }
    ~TempFolder() {
        std::error_code error;
        fs::remove_all(path, error);
    }
};

std::string first_element_name(const fs::path& db_path) {
    data::Database db(db_path.string());
    auto stmt = db.prepare("SELECT name FROM elements ORDER BY id LIMIT 1;");
    return stmt.step() ? stmt.column_text(0) : std::string();
}

// A brewery database at `path` with one fermenter, FV1.
struct Brewery {
    data::Database db;
    app::Session session;
    data::SettingsRepository settings{db};
    app::FloorService floor;
    app::BackupService backups;
    std::int64_t fermenter = 0;

    explicit Brewery(const fs::path& path)
        : db(path.string()),
          session((data::migrate(db), app::open_local_session(db))),
          floor(db, session),
          backups(db, session, settings, path) {
        fermenter = floor.add(domain::ElementType::Fermenter, 0.0, 0.0);
        floor.rename(fermenter, "FV1");
    }
};

} // namespace

TEST_CASE("a backup is a complete copy of the data, and only the newest are kept") {
    TempFolder temp;
    Brewery brewery(temp.path / "ninkasi.db");

    const fs::path first = brewery.backups.back_up_now();
    CHECK(first.parent_path() == temp.path / "backups");
    CHECK(first.filename().string().starts_with("ninkasi-"));
    CHECK(first_element_name(first) == "FV1");

    // Another the same minute gets its own name.
    const fs::path second = brewery.backups.back_up_now();
    CHECK(second != first);
    CHECK(brewery.backups.backups().size() == 2);

    brewery.backups.set_keep(1);
    const fs::path third = brewery.backups.back_up_now();
    REQUIRE(brewery.backups.backups().size() == 1);
    CHECK(brewery.backups.backups().front().path == third);

    // One a day: there's already one today.
    CHECK_FALSE(brewery.backups.back_up_if_due().has_value());

    // A copy saved anywhere, replacing what's there.
    const fs::path copy = temp.path / "copy.db";
    std::ofstream(copy) << "old";
    brewery.backups.save_copy(copy);
    CHECK(first_element_name(copy) == "FV1");
}

TEST_CASE("automatic backups are made once a day when on") {
    TempFolder temp;
    Brewery brewery(temp.path / "ninkasi.db");
    brewery.backups.set_automatic(false);
    CHECK_FALSE(brewery.backups.back_up_if_due().has_value());
    brewery.backups.set_automatic(true);
    CHECK(brewery.backups.back_up_if_due().has_value());
    CHECK_FALSE(brewery.backups.back_up_if_due().has_value());

    // The settings are remembered.
    app::BackupService again(brewery.db, brewery.session, brewery.settings, temp.path / "ninkasi.db");
    CHECK(again.automatic());
    CHECK(again.folder() == temp.path / "backups");
}

TEST_CASE("a restore happens at the next start and keeps the data it replaced") {
    TempFolder temp;
    const fs::path db_path = temp.path / "ninkasi.db";
    fs::path backup;
    {
        Brewery brewery(db_path);
        backup = brewery.backups.back_up_now();
        brewery.floor.rename(brewery.fermenter, "Tank A");  // after the backup
        brewery.backups.restore_on_next_start(backup);
        CHECK(brewery.backups.pending_restore() == backup);
    }

    const std::optional<std::string> message = app::restore_pending_backup(db_path);
    REQUIRE(message.has_value());
    CHECK(first_element_name(db_path) == "FV1");

    // The data it replaced was kept.
    fs::path before;
    for (const fs::directory_entry& entry : fs::directory_iterator(temp.path / "backups")) {
        if (entry.path().filename().string().starts_with("before-restore-")) before = entry.path();
    }
    REQUIRE_FALSE(before.empty());
    CHECK(first_element_name(before) == "Tank A");
    CHECK_FALSE(app::restore_pending_backup(db_path).has_value());  // only once
}

TEST_CASE("a file that isn't a backup isn't restored") {
    TempFolder temp;
    const fs::path db_path = temp.path / "ninkasi.db";
    const fs::path junk = temp.path / "junk.db";
    std::ofstream(junk) << "this is not a database, it's a shopping list";
    {
        Brewery brewery(db_path);
        brewery.backups.restore_on_next_start(junk);
    }
    CHECK_THROWS_AS(app::restore_pending_backup(db_path), std::runtime_error);
    CHECK(first_element_name(db_path) == "FV1");
    CHECK_FALSE(app::restore_pending_backup(db_path).has_value());
}
