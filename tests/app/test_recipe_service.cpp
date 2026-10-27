#include "app/calc_settings.h"
#include "app/ingredient_library.h"
#include "app/recipe_service.h"
#include "app/session.h"
#include "data/database.h"
#include "data/migrations.h"
#include "data/settings_repository.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <stdexcept>
#include <string>

using namespace ninkasi;

namespace {

struct Brewery {
    data::Database db{":memory:"};
    app::Session session;

    Brewery() {
        data::migrate(db);
        session = app::open_local_session(db);
    }

    std::int64_t count(const std::string& sql) {
        auto stmt = db.prepare(sql);
        stmt.step();
        return stmt.column_int64(0);
    }
};

} // namespace

TEST_CASE("recipes are created, listed, saved, copied and deleted") {
    Brewery brewery;
    app::RecipeService recipes(brewery.db, brewery.session);

    domain::Recipe ipa = recipes.new_recipe("West Coast IPA");
    ipa.style = "American IPA";
    const std::int64_t id = recipes.create(ipa);
    recipes.create(recipes.new_recipe("Amber"));

    const auto list = recipes.list();
    REQUIRE(list.size() == 2);
    CHECK(list[0].name == "Amber");  // by name
    CHECK(list[1].name == "West Coast IPA");
    CHECK(list[1].style == "American IPA");

    std::optional<domain::Recipe> loaded = recipes.load(id);
    REQUIRE(loaded.has_value());
    CHECK(loaded->id == id);
    CHECK(loaded->name == "West Coast IPA");

    loaded->efficiency = 0.68;
    recipes.save(*loaded);
    CHECK(recipes.load(id)->efficiency == 0.68);

    const std::int64_t copy = recipes.duplicate(id);
    CHECK(recipes.load(copy)->name == "West Coast IPA (copy)");
    CHECK(recipes.load(copy)->efficiency == 0.68);

    recipes.remove(id);
    CHECK_FALSE(recipes.load(id).has_value());
    CHECK(recipes.list().size() == 2);

    const std::string user = " AND user_id = " + std::to_string(brewery.session.user_id);
    CHECK(brewery.count("SELECT COUNT(*) FROM audit_log WHERE action = 'recipe.create'" + user) == 3);
    CHECK(brewery.count("SELECT COUNT(*) FROM audit_log WHERE action = 'recipe.update'" + user) == 1);
    CHECK(brewery.count("SELECT COUNT(*) FROM audit_log WHERE action = 'recipe.delete'" + user) == 1);
}

TEST_CASE("recipes need a name") {
    Brewery brewery;
    app::RecipeService recipes(brewery.db, brewery.session);
    CHECK_THROWS_AS(recipes.create(recipes.new_recipe("  ")), std::invalid_argument);
    CHECK(recipes.list().empty());
}

TEST_CASE("new recipes start from the brewery's defaults") {
    Brewery brewery;
    app::RecipeService recipes(brewery.db, brewery.session);
    CHECK(recipes.new_recipe("A").batch_volume_l == app::built_in_recipe_defaults().batch_volume_l);

    domain::Recipe defaults = recipes.defaults();
    defaults.batch_volume_l = 500.0;
    defaults.equipment.boil_off_l_per_hr = 33.0;
    defaults.water.source.calcium = 42.0;
    defaults.hops.push_back({});  // ingredients aren't part of the defaults
    recipes.set_defaults(defaults);

    const domain::Recipe fresh = recipes.new_recipe("B");
    CHECK(fresh.name == "B");
    CHECK(fresh.batch_volume_l == 500.0);
    CHECK(fresh.equipment.boil_off_l_per_hr == 33.0);
    CHECK(fresh.water.source.calcium == 42.0);
    CHECK(fresh.hops.empty());
}

TEST_CASE("calculation settings persist") {
    Brewery brewery;
    data::SettingsRepository settings(brewery.db);
    CHECK(app::load_calc_settings(settings) == domain::CalcSettings{});

    domain::CalcSettings chosen;
    chosen.ibu.formula = calc::IbuFormula::Rager;
    chosen.color_model = calc::ColorModel::Daniels;
    app::save_calc_settings(settings, chosen);
    CHECK(app::load_calc_settings(settings) == chosen);

    settings.set("calc.settings", "{broken");
    CHECK(app::load_calc_settings(settings) == domain::CalcSettings{});
}

TEST_CASE("the ingredient library starts with a starter set, once") {
    Brewery brewery;
    std::size_t fermentable_count = 0;
    {
        app::IngredientLibrary library(brewery.db, brewery.session);
        fermentable_count = library.fermentables().size();
        CHECK(fermentable_count > 10);
        CHECK(library.hops().size() > 10);
        CHECK(library.yeasts().size() > 5);

        // Delete everything...
        std::vector<std::int64_t> ids;
        for (const auto& f : library.fermentables()) ids.push_back(f.id);
        for (const auto& h : library.hops()) ids.push_back(h.id);
        for (const auto& y : library.yeasts()) ids.push_back(y.id);
        for (std::int64_t id : ids) library.remove(id);
        CHECK(library.fermentables().empty());
    }
    // ...and it stays deleted.
    app::IngredientLibrary reopened(brewery.db, brewery.session);
    CHECK(reopened.fermentables().empty());
    CHECK(reopened.hops().empty());
}

TEST_CASE("library ingredients are added, updated and removed") {
    Brewery brewery;
    app::IngredientLibrary library(brewery.db, brewery.session);

    domain::Hop hop{"Zappa", "US", calc::HopForm::Pellet, 0.065};
    const std::int64_t id = library.add(hop);
    hop.alpha_acid = 0.07;
    library.update(id, hop);

    app::IngredientLibrary reopened(brewery.db, brewery.session);
    const auto& hops = reopened.hops();
    const auto found = std::find_if(hops.begin(), hops.end(), [id](const auto& h) { return h.id == id; });
    REQUIRE(found != hops.end());
    CHECK(found->item == hop);

    CHECK_THROWS_AS(reopened.add(domain::Hop{}), std::invalid_argument);  // blank name
    CHECK_THROWS_AS(reopened.update(999999, hop), std::invalid_argument);

    reopened.remove(id);
    CHECK(std::none_of(reopened.hops().begin(), reopened.hops().end(), [id](const auto& h) { return h.id == id; }));
    const std::string where = " AND entity = 'ingredient' AND entity_id = " + std::to_string(id);
    CHECK(brewery.count("SELECT COUNT(*) FROM audit_log WHERE action LIKE 'ingredient.%'" + where) == 3);
}
