#include "calc/extract.h"
#include "calc/gravity.h"
#include "calc/yeast.h"
#include "domain/recipe_math.h"
#include "test_helpers.h"

#include <cmath>

using namespace ninkasi;
using namespace ninkasi::domain;

namespace {

Fermentable grain(const char* name, double yield, double lovibond, calc::MaltCategory category) {
    Fermentable f;
    f.name = name;
    f.type = FermentableType::Grain;
    f.category = category;
    f.yield = yield;
    f.color_lovibond = lovibond;
    return f;
}

HopAddition hop(double grams, calc::HopUse use, double minutes) {
    HopAddition h;
    h.hop.name = "Citra";
    h.hop.alpha_acid = 0.12;
    h.mass_kg = grams / 1000.0;
    h.use = use;
    h.time_min = minutes;
    return h;
}

// A 20 L pale ale with a little sugar, a whirlpool and a dry hop.
Recipe pale_ale() {
    Recipe r;
    r.name = "Pale ale";
    r.batch_volume_l = 20.0;
    r.boil_time_min = 60.0;
    r.efficiency = 0.75;
    r.equipment.boil_off_l_per_hr = 4.0;
    r.equipment.kettle_loss_l = 2.0;
    r.equipment.mash_tun_loss_l = 1.0;

    r.fermentables.push_back({0, grain("Pale 2-row", 0.79, 1.8, calc::MaltCategory::Base), 4.5});
    r.fermentables.push_back({0, grain("Crystal 60", 0.74, 60.0, calc::MaltCategory::Crystal), 0.5});
    Fermentable dextrose;
    dextrose.name = "Dextrose";
    dextrose.type = FermentableType::Sugar;
    dextrose.yield = 0.91;
    dextrose.color_lovibond = 0.0;
    r.fermentables.push_back({0, dextrose, 0.3});

    r.hops.push_back(hop(30, calc::HopUse::Boil, 60));
    HopAddition whirlpool = hop(30, calc::HopUse::Whirlpool, 20);
    whirlpool.temperature_c = 80.0;
    r.hops.push_back(whirlpool);
    r.hops.push_back(hop(50, calc::HopUse::DryHop, 0));

    YeastAddition yeast;
    yeast.yeast.name = "American ale";
    yeast.yeast.attenuation = 0.76;
    yeast.packs = 2.0;
    yeast.viability = 0.9;
    r.yeasts.push_back(yeast);

    r.water.source = {.calcium = 20, .bicarbonate = 50};
    r.water.salts = {{calc::Salt::Gypsum, 0.005}, {calc::Salt::CalciumChloride, 0.003}};
    r.water.mash_acids = {{calc::Acid::Lactic88, 0.002}};
    return r;
}

} // namespace

TEST_CASE("volumes come from the batch size, equipment and grain bill") {
    const Recipe recipe = pale_ale();
    const RecipeStats stats = calculate(recipe, {});

    CHECK(stats.grain_mass_kg == near(5.0, 1e-12));  // the sugar isn't mashed
    calc::BrewVolumeInputs inputs;
    inputs.fermenter_volume_l = 20.0;
    inputs.kettle_loss_l = 2.0;
    inputs.boil_off_l_per_hr = 4.0;
    inputs.boil_time_min = 60.0;
    inputs.grain_mass_kg = 5.0;
    inputs.mash_tun_loss_l = 1.0;
    const calc::BrewVolumes expected = calc::plan_brew_volumes(inputs);
    CHECK(stats.volumes.total_water_l == near(expected.total_water_l, 1e-12));
    CHECK(stats.volumes.knockout_volume_l == near(22.0, 1e-12));
}

TEST_CASE("efficiency is the share of mashed extract that reaches the fermenter") {
    Recipe all_grain = pale_ale();
    all_grain.fermentables.pop_back();  // drop the sugar
    const RecipeStats stats = calculate(all_grain, {});
    const double potential = 4.5 * 0.79 + 0.5 * 0.74;
    CHECK(calc::extract_kg(20.0, stats.og) / potential == near(0.75, 1e-9));
    // More of it is in the kettle at knockout, before kettle losses.
    CHECK(stats.kettle_efficiency == near(0.75 * 22.0 / 20.0, 1e-12));

    // Sugar adds its full yield to the kettle.
    const RecipeStats with_sugar = calculate(pale_ale(), {});
    const double kettle_extract = potential * with_sugar.kettle_efficiency + 0.3 * 0.91;
    CHECK(calc::extract_kg(22.0, with_sugar.og) == near(kettle_extract, 1e-9));
    CHECK(with_sugar.og == near(1.061, 0.005));
    CHECK(with_sugar.pre_boil_gravity < with_sugar.og);
}

TEST_CASE("fermentation figures follow the first yeast") {
    const RecipeStats stats = calculate(pale_ale(), {});
    CHECK(stats.attenuation == 0.76);
    CHECK(stats.fg == near(calc::predict_fg(stats.og, 0.76), 1e-12));
    CHECK(stats.abv == near(calc::abv(stats.og, stats.fg, calc::AbvFormula::Simple), 1e-12));

    CalcSettings balling;
    balling.abv_formula = calc::AbvFormula::Balling;
    const RecipeStats with_balling = calculate(pale_ale(), balling);
    CHECK(with_balling.abv == near(calc::abv(stats.og, stats.fg, calc::AbvFormula::Balling), 1e-12));
}

TEST_CASE("IBUs use the chosen boil gravity and the knockout volume") {
    const Recipe recipe = pale_ale();
    const RecipeStats stats = calculate(recipe, {});
    REQUIRE(stats.hop_ibus.size() == 3);
    CHECK(stats.hop_ibus[2] == 0.0);  // dry hop

    const calc::HopAddition hops[] = {
        {0.030, 0.12, calc::HopUse::Boil, calc::HopForm::Pellet, 60.0, 100.0},
        {0.030, 0.12, calc::HopUse::Whirlpool, calc::HopForm::Pellet, 20.0, 80.0},
    };
    calc::BoilConditions boil;
    boil.boil_gravity_sg = (stats.pre_boil_gravity + stats.og) / 2.0;
    boil.volume_l = 22.0;
    boil.boil_volume_l = stats.volumes.pre_boil_volume_l;
    CHECK(stats.ibu == near(calc::total_ibu(hops, boil), 1e-9));
    CHECK(stats.bu_gu == near(stats.ibu / calc::gravity_points(stats.og), 1e-12));

    // Using the (higher) post-boil gravity lowers utilization.
    CalcSettings post_boil;
    post_boil.boil_gravity = BoilGravity::PostBoil;
    CHECK(calculate(recipe, post_boil).ibu < stats.ibu);
}

TEST_CASE("color uses every fermentable in the knockout volume") {
    const RecipeStats stats = calculate(pale_ale(), {});
    const calc::GrainColor colors[] = {{4.5, 1.8}, {0.5, 60.0}, {0.3, 0.0}};
    CHECK(stats.srm == near(calc::estimate_srm(colors, 22.0), 1e-12));
}

TEST_CASE("yeast cells pitched, needed, and grown in a starter") {
    Recipe recipe = pale_ale();
    RecipeStats stats = calculate(recipe, {});
    CHECK(stats.cells_pitched_billion == near(2 * 100 * 0.9, 1e-9));
    CHECK(stats.cells_available_billion == stats.cells_pitched_billion);
    CHECK(stats.cells_needed_billion == near(calc::cells_needed_billion(20.0, stats.og, 0.75), 1e-9));

    recipe.starter.enabled = true;
    stats = calculate(recipe, {});
    CHECK(stats.cells_available_billion ==
          near(calc::starter_cells_billion(180.0, 2.0, 1.036, calc::StarterMethod::StirPlate), 1e-9));

    YeastAddition dry;
    dry.yeast.form = YeastForm::Dry;
    dry.yeast.cells_per_gram_billion = 10.0;
    dry.mass_kg = 0.0115;
    recipe.yeasts = {dry};
    recipe.starter.enabled = false;
    CHECK(calculate(recipe, {}).cells_pitched_billion == near(115.0, 1e-9));
}

TEST_CASE("water: salts in all the water, mash pH from the strike water") {
    const Recipe recipe = pale_ale();
    const RecipeStats stats = calculate(recipe, {});
    CHECK(stats.mash_water == calc::add_salts(recipe.water.source, recipe.water.salts, stats.volumes.total_water_l));
    CHECK(stats.residual_alkalinity == near(calc::residual_alkalinity(stats.mash_water), 1e-12));

    const calc::MashGrain grist[] = {
        {4.5, calc::MaltCategory::Base, 1.8, std::nullopt, std::nullopt},
        {0.5, calc::MaltCategory::Crystal, 60.0, std::nullopt, std::nullopt},
    };
    CHECK(stats.mash_ph ==
          near(calc::estimate_mash_ph(grist, stats.mash_water, stats.volumes.strike_water_l, recipe.water.mash_acids),
               1e-12));
    CHECK(stats.mash_ph > 5.0);
    CHECK(stats.mash_ph < 5.8);
}

TEST_CASE("an extract recipe has no mash") {
    Recipe recipe;
    recipe.batch_volume_l = 20.0;
    Fermentable extract;
    extract.type = FermentableType::DryExtract;
    extract.yield = 0.95;
    recipe.fermentables.push_back({0, extract, 3.0});
    const RecipeStats stats = calculate(recipe, {});
    CHECK(stats.grain_mass_kg == 0.0);
    CHECK(stats.og > 1.040);
    CHECK(std::isnan(stats.mash_ph));
}

TEST_CASE("a recipe without a batch size has no numbers") {
    Recipe recipe = pale_ale();
    recipe.batch_volume_l = 0.0;
    const RecipeStats stats = calculate(recipe, {});
    CHECK(std::isnan(stats.og));
    CHECK(std::isnan(stats.ibu));
}
