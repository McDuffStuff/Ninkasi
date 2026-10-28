#include "data/database.h"
#include "data/recipe_json.h"

#include <doctest/doctest.h>

using namespace ninkasi;
using namespace ninkasi::domain;

namespace {

// A recipe that sets every field to something other than its default.
Recipe everything() {
    Recipe r;
    r.name = "Everything IPA";
    r.style = "American IPA";
    r.author = "Local Brewer";
    r.notes = "Line one\nLine \"two\"";
    r.batch_volume_l = 1173.5;
    r.boil_time_min = 75;
    r.efficiency = 0.81;
    r.pitch_rate = 1.0;
    r.equipment = {50.0, 45.0, 0.035, 0.9, 15.0, 2.8, 0.7};

    Fermentable malt;
    malt.name = "Maris Otter";
    malt.supplier = "Crisp";
    malt.type = FermentableType::Grain;
    malt.category = calc::MaltCategory::Crystal;
    malt.yield = 0.78;
    malt.color_lovibond = 3.5;
    malt.di_ph = 5.62;
    r.fermentables.push_back({12, malt, 250.0});

    HopAddition hop;
    hop.library_id = 4;
    hop.hop = {"Galaxy", "AU", calc::HopForm::Leaf, 0.141};
    hop.mass_kg = 1.2;
    hop.use = calc::HopUse::Whirlpool;
    hop.time_min = 25;
    hop.temperature_c = 82;
    r.hops.push_back(hop);

    YeastAddition yeast;
    yeast.library_id = 9;
    yeast.yeast = {"House ale", "Lab", "HA-1", YeastType::Lager, YeastForm::Slurry, 0.81, 200, 15};
    yeast.packs = 3;
    yeast.mass_kg = 0.5;
    yeast.slurry_l = 4.0;
    yeast.slurry_cells_per_ml_billion = 1.5;
    yeast.viability = 0.85;
    r.yeasts.push_back(yeast);

    r.miscs.push_back({"Whirlfloc", "1 tablet", MiscUse::Whirlpool, 5});
    r.mash_steps = {{"Protein rest", 52, 15}, {"Saccharification", 66, 60}};
    r.water.source = {30, 8, 12, 25, 40, 110};
    r.water.source_name = "City tap";
    r.water.salts = {{calc::Salt::Gypsum, 0.12}, {calc::Salt::Chalk, 0.01}};
    r.water.mash_acids = {{calc::Acid::Phosphoric10, 0.25}};
    r.water.target_mash_ph = 5.3;
    r.starter = {true, 4.0, 1.040, calc::StarterMethod::Shaken};
    return r;
}

} // namespace

TEST_CASE("a recipe survives a round trip through JSON") {
    const Recipe original = everything();
    const Recipe copy = data::recipe_from_json(data::to_json(original));
    CHECK(copy == original);
}

TEST_CASE("missing fields take their defaults and unknown ones are ignored") {
    const Recipe r = data::recipe_from_json(R"({
        "name": "Sparse",
        "from_the_future": {"x": 1},
        "hops": [{"hop": {"name": "Mystery", "form": "teleported"}, "use": "warp"}],
        "water": {"salts": [{"salt": "unobtainium", "mass_kg": 1}]}
    })");
    CHECK(r.name == "Sparse");
    CHECK(r.boil_time_min == Recipe{}.boil_time_min);
    REQUIRE(r.hops.size() == 1);
    CHECK(r.hops[0].hop.name == "Mystery");
    CHECK(r.hops[0].hop.form == calc::HopForm::Pellet);  // unknown key -> default
    CHECK(r.hops[0].use == calc::HopUse::Boil);
    CHECK(r.water.salts.empty());
}

TEST_CASE("malformed JSON is reported as a database error") {
    CHECK_THROWS_AS(data::recipe_from_json("{not json"), data::DbError);
    CHECK_THROWS_AS(data::recipe_from_json("[1, 2]"), data::DbError);
    CHECK_THROWS_AS(data::recipe_from_json(R"({"batch_volume_l": "lots"})"), data::DbError);
}

TEST_CASE("library ingredients and calculation settings round-trip") {
    const Recipe r = everything();
    CHECK(data::fermentable_from_json(data::to_json(r.fermentables[0].fermentable)) == r.fermentables[0].fermentable);
    CHECK(data::hop_from_json(data::to_json(r.hops[0].hop)) == r.hops[0].hop);
    CHECK(data::yeast_from_json(data::to_json(r.yeasts[0].yeast)) == r.yeasts[0].yeast);

    CalcSettings settings;
    settings.ibu.formula = calc::IbuFormula::Garetz;
    settings.ibu.utilization_scale = 0.9;
    settings.ibu.form_factor[static_cast<std::size_t>(calc::HopForm::Pellet)] = 1.1;
    settings.ibu.first_wort_factor = 1.1;
    settings.ibu.mash_hop_factor = 0.3;
    settings.ibu.elevation_m = 1600;
    settings.boil_gravity = BoilGravity::PostBoil;
    settings.color_model = calc::ColorModel::Mosher;
    settings.abv_formula = calc::AbvFormula::Balling;
    CHECK(data::calc_settings_from_json(data::to_json(settings)) == settings);
}
