#include "calc/alcohol.h"
#include "calc/bitterness.h"
#include "calc/color.h"
#include "calc/extract.h"
#include "calc/formulas.h"
#include "calc/gravity.h"
#include "calc/volumes.h"
#include "calc/water.h"
#include "calc/yeast.h"
#include "test_helpers.h"

#include <cmath>
#include <set>
#include <string>
#include <vector>

using namespace ninkasi;
using calc::Formula;

namespace {

// Puts the original formulas back when a test ends, whatever it adjusted.
struct OriginalFormulas {
    OriginalFormulas() { calc::clear_adjusted_formulas(); }
    ~OriginalFormulas() { calc::clear_adjusted_formulas(); }
};

// Many calculations through the public calc API, covering every adjustable
// formula: compare two runs to see whether a formula's version changed anything.
std::vector<double> probe() {
    std::vector<double> out;
    for (double sg : {1.000, 1.035, 1.050, 1.080, 1.110}) out.push_back(calc::sg_to_plato(sg));
    for (double plato : {0.0, 8.0, 12.0, 20.0}) out.push_back(calc::plato_to_sg(plato));
    out.push_back(calc::hydrometer_corrected_sg(1.050, 30.0, 20.0));
    out.push_back(calc::hydrometer_corrected_sg(1.040, 10.0, 15.6));
    out.push_back(calc::refractometer_wort_sg(12.5, 1.04));
    out.push_back(calc::refractometer_fermenting_sg(12.0, 6.5, 1.04));
    out.push_back(calc::wort_correction_factor(13.0, 1.050));

    for (auto formula : {calc::AbvFormula::Simple, calc::AbvFormula::Alternate, calc::AbvFormula::Balling}) {
        out.push_back(calc::abv(1.050, 1.010, formula));
        out.push_back(calc::abv(1.090, 1.018, formula));
    }
    out.push_back(calc::real_extract_plato(1.060, 1.012));
    out.push_back(calc::apparent_attenuation(1.060, 1.012));
    out.push_back(calc::real_attenuation(1.060, 1.012));
    out.push_back(calc::predict_fg(1.060, 0.78));

    out.push_back(calc::tinseth_utilization(1.055, 60.0));
    out.push_back(calc::rager_utilization(1.070, 45.0));
    out.push_back(calc::rager_utilization(1.040, 15.0));
    out.push_back(calc::whirlpool_temperature_factor(80.0));
    const calc::HopAddition hops[] = {
        {0.5, 0.12, calc::HopUse::Boil, calc::HopForm::Pellet, 60.0},
        {0.3, 0.08, calc::HopUse::Whirlpool, calc::HopForm::Leaf, 20.0, 80.0},
        {0.2, 0.10, calc::HopUse::FirstWort, calc::HopForm::Pellet, 0.0},
    };
    const calc::BoilConditions boil{1.065, 1170.0, 1300.0, 60.0};
    for (auto formula : {calc::IbuFormula::Tinseth, calc::IbuFormula::Rager, calc::IbuFormula::Garetz}) {
        calc::IbuSettings settings;
        settings.formula = formula;
        settings.elevation_m = 1500.0;
        for (double ibu : calc::addition_ibus(hops, boil, settings)) out.push_back(ibu);
    }

    for (double mcu : {3.0, 10.0, 40.0}) {
        for (auto model : {calc::ColorModel::Morey, calc::ColorModel::Daniels, calc::ColorModel::Mosher}) {
            out.push_back(calc::srm_from_mcu(mcu, model));
        }
    }

    out.push_back(calc::extract_kg(1170.0, 1.050));
    out.push_back(calc::sg_from_extract(150.0, 1170.0));
    out.push_back(calc::efficiency(150.0, 200.0));
    out.push_back(calc::as_is_yield(0.81, 0.04, 0.015));
    out.push_back(calc::potential_sg_from_yield(0.78));

    calc::BrewVolumeInputs in;
    in.fermenter_volume_l = 1170.0;
    in.kettle_loss_l = 40.0;
    in.boil_off_l_per_hr = 80.0;
    in.grain_mass_kg = 250.0;
    in.mash_tun_loss_l = 20.0;
    in.mash_thickness_l_per_kg = 2.6;
    const calc::BrewVolumes v = calc::plan_brew_volumes(in);
    for (double volume : {v.strike_water_l, v.sparge_water_l, v.total_water_l, v.mash_volume_l, v.pre_boil_volume_l,
                          v.post_boil_volume_l, v.knockout_volume_l}) {
        out.push_back(volume);
    }

    const calc::WaterProfile source{40.0, 8.0, 10.0, 30.0, 50.0, 150.0};
    std::vector<calc::SaltAddition> salts;
    for (const calc::SaltInfo& salt : calc::salts()) salts.push_back({salt.salt, 0.2});
    const calc::WaterProfile water = calc::add_salts(source, salts, 1600.0);
    for (double ion : {water.calcium, water.magnesium, water.sodium, water.chloride, water.sulfate, water.bicarbonate}) {
        out.push_back(ion);
    }
    out.push_back(calc::alkalinity(water));
    out.push_back(calc::residual_alkalinity(water));
    out.push_back(calc::sulfate_chloride_ratio(water));
    for (double lovibond : {5.0, 40.0, 120.0}) out.push_back(calc::default_di_ph(calc::MaltCategory::Crystal, lovibond));

    out.push_back(calc::cells_needed_billion(1170.0, 1.050, 0.75));
    out.push_back(calc::liquid_yeast_viability(30.0));
    out.push_back(calc::liquid_yeast_viability(200.0));
    // Starters that hit each branch of the growth models.
    for (double cells : {50.0, 200.0, 400.0}) {
        for (auto method : {calc::StarterMethod::StirPlate, calc::StarterMethod::Shaken,
                            calc::StarterMethod::NoAgitation}) {
            out.push_back(calc::starter_cells_billion(cells, 1.5, 1.040, method));
        }
    }
    return out;
}

bool same(const std::vector<double>& a, const std::vector<double>& b, double tolerance) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::isnan(a[i]) && std::isnan(b[i])) continue;
        if (!(std::abs(a[i] - b[i]) <= tolerance * std::max(1.0, std::abs(a[i])))) return false;
    }
    return true;
}

} // namespace

TEST_CASE("every formula has a unique key, and the reference ones are marked") {
    std::set<std::string_view> keys;
    for (const calc::FormulaInfo& formula : calc::formulas()) {
        CHECK(keys.insert(formula.key).second);
        CHECK(calc::formula_from_key(formula.key) == formula.formula);
        CHECK(&calc::info(formula.formula) == &formula);
        CHECK_FALSE(formula.name.empty());
        // Every formula can be shown.
        CHECK_NOTHROW(calc::parse_latex(formula.latex));
        CHECK_NOTHROW(calc::parse_latex(formula.result));
    }
    CHECK_FALSE(calc::formula_from_key("no_such_formula"));
    CHECK_THROWS_AS(calc::compile_formula(Formula::MashPh, "1"), calc::FormulaError);
}

TEST_CASE("each original formula, run as an adjustment, gives the built-in results") {
    OriginalFormulas originals;
    const std::vector<double> built_in = probe();
    for (const calc::FormulaInfo& formula : calc::formulas()) {
        if (!formula.adjustable) continue;
        CAPTURE(formula.key);
        calc::set_adjusted_formula(formula.formula, calc::compile_formula(formula.formula, formula.latex));
        CHECK(same(built_in, probe(), 1e-9));
        calc::set_adjusted_formula(formula.formula, std::nullopt);
    }
}

TEST_CASE("every adjustable formula is used by some calculation") {
    OriginalFormulas originals;
    const std::vector<double> built_in = probe();
    for (const calc::FormulaInfo& formula : calc::formulas()) {
        if (!formula.adjustable) continue;
        CAPTURE(formula.key);
        // The original plus a little: something somewhere should change.
        const std::string nudged = "\\left(" + std::string(formula.latex) + "\\right) \\cdot 1.01 + 0.01";
        calc::set_adjusted_formula(formula.formula, calc::compile_formula(formula.formula, nudged));
        CHECK_FALSE(same(built_in, probe(), 1e-9));
        calc::set_adjusted_formula(formula.formula, std::nullopt);
    }
}

TEST_CASE("an adjusted formula is used until it's put back") {
    OriginalFormulas originals;
    CHECK(calc::abv(1.050, 1.010) == near(0.0525, 1e-12));
    calc::set_adjusted_formula(Formula::AbvSimple, calc::compile_formula(Formula::AbvSimple, "(OG - FG) \\times 135"));
    CHECK(calc::abv(1.050, 1.010) == near(0.054, 1e-12));
    calc::set_adjusted_formula(Formula::AbvSimple, std::nullopt);
    CHECK(calc::abv(1.050, 1.010) == near(0.0525, 1e-12));

    // Conversions follow an adjusted °Plato formula both ways.
    calc::set_adjusted_formula(Formula::SgToPlato, calc::compile_formula(Formula::SgToPlato, "(SG - 1) \\times 250"));
    CHECK(calc::sg_to_plato(1.040) == near(10.0, 1e-9));
    CHECK(calc::plato_to_sg(10.0) == near(1.040, 1e-9));
    calc::clear_adjusted_formulas();
    CHECK(calc::sg_to_plato(1.040) == near(9.99, 0.01));
}

TEST_CASE("the examples give sensible results with the original formulas") {
    for (const calc::FormulaInfo& formula : calc::formulas()) {
        if (!formula.adjustable) continue;
        CAPTURE(formula.key);
        std::vector<double> examples;
        for (const calc::FormulaInput& input : formula.inputs) examples.push_back(input.example);
        CHECK(std::isfinite(calc::compile_formula(formula.formula, formula.latex).evaluate(examples)));
    }
}
