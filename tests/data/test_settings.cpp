#include "data/database.h"
#include "data/migrations.h"
#include "data/settings_repository.h"

#include <doctest/doctest.h>

using namespace ninkasi;

TEST_CASE("SettingsRepository stores and overwrites values") {
    data::Database db(":memory:");
    data::migrate(db);
    data::SettingsRepository settings(db);

    CHECK_FALSE(settings.get("ui.theme").has_value());
    CHECK(settings.get_or("ui.theme", "dark") == "dark");

    settings.set("ui.theme", "light");
    CHECK(settings.get("ui.theme") == "light");

    settings.set("ui.theme", "dark");
    CHECK(settings.get("ui.theme") == "dark");
}
