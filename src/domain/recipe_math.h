#pragma once
// Everything Ninkasi works out from a recipe: brew-day volumes, gravities,
// alcohol, bitterness, color, yeast and water.

#include "calc/alcohol.h"
#include "calc/bitterness.h"
#include "calc/color.h"
#include "calc/volumes.h"
#include "calc/water.h"
#include "domain/recipe.h"

#include <vector>

namespace ninkasi::domain {

// Which wort gravity the IBU formulas see.
enum class BoilGravity { PreBoil, Average, PostBoil };

// The brewery's choice of formulas, used for every recipe.
struct CalcSettings {
    calc::IbuSettings ibu;
    BoilGravity boil_gravity = BoilGravity::Average;
    calc::ColorModel color_model = calc::ColorModel::Morey;
    calc::AbvFormula abv_formula = calc::AbvFormula::Simple;

    bool operator==(const CalcSettings&) const = default;
};

struct RecipeStats {
    calc::BrewVolumes volumes;
    double grain_mass_kg = 0.0;         // mashed fermentables
    double potential_extract_kg = 0.0;  // all fermentables

    double pre_boil_gravity = 0.0;
    double og = 0.0;
    double fg = 0.0;
    double attenuation = 0.0;           // apparent, from the first yeast
    double abv = 0.0;
    double ibu = 0.0;
    std::vector<double> hop_ibus;       // per hop addition, in recipe order
    double bu_gu = 0.0;
    double srm = 0.0;
    // Mashed extract in the knockout wort vs. its potential: the brewhouse
    // yield measured at knockout, before kettle losses.
    double kettle_efficiency = 0.0;

    double cells_needed_billion = 0.0;
    double cells_pitched_billion = 0.0;    // from the packs, sachets or slurry
    double cells_available_billion = 0.0;  // after the starter, if any

    calc::WaterProfile mash_water;      // source water plus salts
    double residual_alkalinity = 0.0;
    double sulfate_chloride_ratio = 0.0;
    double mash_ph = 0.0;
    double acid_to_target_meq = 0.0;    // more acid needed to reach the target pH (negative: base)
};

// How the numbers fit together:
// - Volumes work back from the batch size (calc/volumes.h).
// - Efficiency is the share of mashed extract that reaches the fermenter, so
//   the knockout wort holds that much divided by the share of it that reaches
//   the fermenter. Sugars and extracts dissolve fully in the kettle.
// - OG is the knockout wort's gravity. All fermentables are assumed to be in
//   the kettle at the start of the boil for the pre-boil gravity.
// - Color and IBUs are for the knockout volume; FG uses the first yeast's
//   attenuation (75 % if there is no yeast).
// - Salts dissolve in all the brewing water; the mash pH uses the strike water.
RecipeStats calculate(const Recipe& recipe, const CalcSettings& settings);

} // namespace ninkasi::domain
