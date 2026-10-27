#include "domain/recipe_math.h"

#include "calc/common.h"
#include "calc/extract.h"
#include "calc/gravity.h"
#include "calc/yeast.h"

#include <numeric>

namespace ninkasi::domain {

namespace {

double cells_in(const YeastAddition& addition) {
    const Yeast& yeast = addition.yeast;
    double cells = 0.0;
    switch (yeast.form) {
    case YeastForm::Liquid:
        cells = addition.packs * yeast.cells_per_pack_billion;
        break;
    case YeastForm::Dry:
        cells = addition.mass_kg * 1000.0 * yeast.cells_per_gram_billion;
        break;
    case YeastForm::Slurry:
        cells = addition.slurry_l * 1000.0 * addition.slurry_cells_per_ml_billion;
        break;
    }
    return cells * addition.viability;
}

double boil_gravity(const RecipeStats& stats, BoilGravity basis) {
    switch (basis) {
    case BoilGravity::PreBoil:  return stats.pre_boil_gravity;
    case BoilGravity::Average:  return (stats.pre_boil_gravity + stats.og) / 2.0;
    case BoilGravity::PostBoil: return stats.og;
    }
    return stats.og;
}

} // namespace

RecipeStats calculate(const Recipe& recipe, const CalcSettings& settings) {
    RecipeStats stats;

    // --- Fermentables --------------------------------------------------------
    double mashed_potential_kg = 0.0;
    double kettle_potential_kg = 0.0;
    std::vector<calc::GrainColor> colors;
    std::vector<calc::MashGrain> mash;
    for (const FermentableAddition& addition : recipe.fermentables) {
        const Fermentable& f = addition.fermentable;
        const double potential_kg = addition.mass_kg * f.yield;
        colors.push_back({addition.mass_kg, f.color_lovibond});
        if (is_mashed(f.type)) {
            mashed_potential_kg += potential_kg;
            stats.grain_mass_kg += addition.mass_kg;
            mash.push_back({addition.mass_kg, f.category, f.color_lovibond, f.di_ph, std::nullopt});
        } else {
            kettle_potential_kg += potential_kg;
        }
    }
    stats.potential_extract_kg = mashed_potential_kg + kettle_potential_kg;

    // --- Volumes -------------------------------------------------------------
    const Equipment& equipment = recipe.equipment;
    calc::BrewVolumeInputs inputs;
    inputs.fermenter_volume_l = recipe.batch_volume_l;
    inputs.kettle_loss_l = equipment.kettle_loss_l;
    inputs.cooling_shrinkage = equipment.cooling_shrinkage;
    inputs.boil_off_l_per_hr = equipment.boil_off_l_per_hr;
    inputs.boil_time_min = recipe.boil_time_min;
    inputs.grain_mass_kg = stats.grain_mass_kg;
    inputs.grain_absorption_l_per_kg = equipment.grain_absorption_l_per_kg;
    inputs.mash_tun_loss_l = equipment.mash_tun_loss_l;
    inputs.mash_thickness_l_per_kg = equipment.mash_thickness_l_per_kg;
    inputs.grain_displacement_l_per_kg = equipment.grain_displacement_l_per_kg;
    stats.volumes = calc::plan_brew_volumes(inputs);
    const double knockout_l = stats.volumes.knockout_volume_l;

    // --- Gravities -----------------------------------------------------------
    const double share_to_fermenter = recipe.batch_volume_l / knockout_l;
    stats.kettle_efficiency = recipe.efficiency / share_to_fermenter;
    const double kettle_extract_kg = mashed_potential_kg * stats.kettle_efficiency + kettle_potential_kg;
    stats.og = calc::sg_from_extract(kettle_extract_kg, knockout_l);
    const double pre_boil_cold_l = stats.volumes.pre_boil_volume_l * (1.0 - equipment.cooling_shrinkage);
    stats.pre_boil_gravity = calc::sg_from_extract(kettle_extract_kg, pre_boil_cold_l);

    stats.attenuation = recipe.yeasts.empty() ? 0.75 : recipe.yeasts.front().yeast.attenuation;
    stats.fg = calc::predict_fg(stats.og, stats.attenuation);
    stats.abv = calc::abv(stats.og, stats.fg, settings.abv_formula);

    // --- Bitterness and color ------------------------------------------------
    std::vector<calc::HopAddition> hops;
    for (const HopAddition& addition : recipe.hops) {
        hops.push_back({addition.mass_kg, addition.hop.alpha_acid, addition.use, addition.hop.form, addition.time_min,
                        addition.temperature_c});
    }
    calc::BoilConditions boil;
    boil.boil_gravity_sg = boil_gravity(stats, settings.boil_gravity);
    boil.volume_l = knockout_l;
    boil.boil_volume_l = stats.volumes.pre_boil_volume_l;
    boil.boil_time_min = recipe.boil_time_min;
    stats.hop_ibus = calc::addition_ibus(hops, boil, settings.ibu);
    stats.ibu = std::accumulate(stats.hop_ibus.begin(), stats.hop_ibus.end(), 0.0);
    stats.bu_gu = stats.ibu / calc::gravity_points(stats.og);
    stats.srm = calc::estimate_srm(colors, knockout_l, settings.color_model);

    // --- Yeast ---------------------------------------------------------------
    stats.cells_needed_billion = calc::cells_needed_billion(recipe.batch_volume_l, stats.og, recipe.pitch_rate);
    for (const YeastAddition& addition : recipe.yeasts) stats.cells_pitched_billion += cells_in(addition);
    stats.cells_available_billion = stats.cells_pitched_billion;
    if (recipe.starter.enabled && stats.cells_pitched_billion > 0.0) {
        stats.cells_available_billion = calc::starter_cells_billion(
            stats.cells_pitched_billion, recipe.starter.volume_l, recipe.starter.gravity_sg, recipe.starter.method);
    }

    // --- Water ---------------------------------------------------------------
    const Water& water = recipe.water;
    stats.mash_water = calc::add_salts(water.source, water.salts, stats.volumes.total_water_l);
    stats.residual_alkalinity = calc::residual_alkalinity(stats.mash_water);
    stats.sulfate_chloride_ratio = calc::sulfate_chloride_ratio(stats.mash_water);
    const double strike_l = stats.volumes.strike_water_l;
    stats.mash_ph = calc::estimate_mash_ph(mash, stats.mash_water, strike_l, water.mash_acids);
    stats.acid_to_target_meq =
        calc::acid_needed_meq(mash, stats.mash_water, strike_l, water.mash_acids, water.target_mash_ph);
    return stats;
}

} // namespace ninkasi::domain
