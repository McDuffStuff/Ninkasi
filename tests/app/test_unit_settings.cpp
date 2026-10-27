#include "app/unit_settings.h"
#include "data/database.h"
#include "data/migrations.h"
#include "data/settings_repository.h"

#include <doctest/doctest.h>

using namespace ninkasi;

TEST_CASE("unit preferences default to the US brewery preset") {
    data::Database db(":memory:");
    data::migrate(db);
    data::SettingsRepository settings(db);
    CHECK(app::load_unit_preferences(settings) == calc::UnitPreferences::us_brewery());
}

TEST_CASE("unit preferences round-trip through settings") {
    data::Database db(":memory:");
    data::migrate(db);
    data::SettingsRepository settings(db);

    calc::UnitPreferences chosen = calc::UnitPreferences::metric_brewery();
    chosen.set(calc::Measure::Gravity, calc::Unit::SpecificGravity);
    chosen.set(calc::Measure::HopMass, calc::Unit::Gram);
    app::save_unit_preferences(settings, chosen);

    CHECK(app::load_unit_preferences(settings) == chosen);
    CHECK(settings.get("units.volume") == "hectoliter");
}

TEST_CASE("unknown or mismatched saved units fall back to the default") {
    data::Database db(":memory:");
    data::migrate(db);
    data::SettingsRepository settings(db);
    settings.set("units.volume", "furlong");
    settings.set("units.mass", "us_gallon");  // a volume unit saved for mass
    settings.set("units.temperature", "celsius");

    const calc::UnitPreferences loaded = app::load_unit_preferences(settings);
    CHECK(loaded.unit_for(calc::Measure::Volume) == calc::Unit::UsBarrel);
    CHECK(loaded.unit_for(calc::Measure::Mass) == calc::Unit::Pound);
    CHECK(loaded.unit_for(calc::Measure::Temperature) == calc::Unit::Celsius);
}
