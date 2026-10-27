#include "calc/bitterness.h"
#include "calc/units.h"
#include "test_helpers.h"

#include <cmath>

using namespace ninkasi::calc;

// The reference values below are the published formulas evaluated
// independently for the classic example: 1 oz of 5 % alpha-acid hops boiled
// 60 minutes in 5 US gallons at a boil gravity of 1.050 (74.89 mg/L of alpha
// acids added).

namespace {

HopAddition one_ounce(double minutes, HopUse use = HopUse::Boil) {
    HopAddition hop;
    hop.mass_kg = kKilogramsPerPound / 16.0;
    hop.alpha_acid = 0.05;
    hop.use = use;
    hop.time_min = minutes;
    return hop;
}

BoilConditions five_gallons(double boil_gravity_sg = 1.050) {
    BoilConditions boil;
    boil.boil_gravity_sg = boil_gravity_sg;
    boil.volume_l = 5.0 * kLitersPerUsGallon;
    boil.boil_time_min = 60.0;
    return boil;
}

IbuSettings using_formula(IbuFormula formula) {
    IbuSettings settings;
    settings.formula = formula;
    return settings;
}

} // namespace

TEST_CASE("Tinseth") {
    const HopAddition hops[] = {one_ounce(60.0)};
    CHECK(tinseth_utilization(1.050, 60.0) == near(0.2306641, 1e-7));
    CHECK(total_ibu(hops, five_gallons()) == near(17.2748, 1e-3));

    // Stronger wort lowers utilization; no boil, no isomerization.
    CHECK(tinseth_utilization(1.080, 60.0) < tinseth_utilization(1.050, 60.0));
    CHECK(tinseth_utilization(1.050, 0.0) == 0.0);
}

TEST_CASE("Rager") {
    const HopAddition hops[] = {one_ounce(60.0)};
    CHECK(rager_utilization(1.050, 60.0) == near(0.3081951, 1e-7));
    CHECK(total_ibu(hops, five_gallons(), using_formula(IbuFormula::Rager)) == near(23.0812, 1e-3));
    // Above 1.050 utilization is divided by 1 + (G − 1.050) / 0.2.
    CHECK(total_ibu(hops, five_gallons(1.070), using_formula(IbuFormula::Rager)) == near(20.9829, 1e-3));
    // Below 1.050 there is no adjustment.
    CHECK(rager_utilization(1.030, 60.0) == rager_utilization(1.050, 60.0));
}

TEST_CASE("Garetz") {
    // Utilization table: nothing under 11 minutes, 20 % at 51–60, 23 % from 81.
    CHECK(garetz_table_utilization(10.0) == 0.0);
    CHECK(garetz_table_utilization(11.0) == 0.02);
    CHECK(garetz_table_utilization(15.0) == 0.02);
    CHECK(garetz_table_utilization(16.0) == 0.05);
    CHECK(garetz_table_utilization(60.0) == 0.20);
    CHECK(garetz_table_utilization(61.0) == 0.21);
    CHECK(garetz_table_utilization(120.0) == 0.23);

    const HopAddition hops[] = {one_ounce(60.0)};
    IbuSettings settings = using_formula(IbuFormula::Garetz);
    // Hopping-rate factor solved exactly: 14.98 IBU before it, 14.20 after.
    CHECK(total_ibu(hops, five_gallons(), settings) == near(14.2025, 1e-3));

    // 1000 m elevation: temperature factor 1.119.
    settings.elevation_m = 1000.0;
    CHECK(total_ibu(hops, five_gallons(), settings) == near(12.7560, 1e-3));
}

TEST_CASE("whirlpool temperature factor (Hosom)") {
    CHECK(whirlpool_temperature_factor(100.0) == 1.0);  // capped at the boil
    CHECK(whirlpool_temperature_factor(90.0) == near(0.490654, 1e-5));
    CHECK(whirlpool_temperature_factor(80.0) == near(0.228993, 1e-5));
}

TEST_CASE("hop uses") {
    const BoilConditions boil = five_gallons();
    const HopAddition full_boil[] = {one_ounce(60.0)};
    const double full_boil_ibu = total_ibu(full_boil, boil);

    SUBCASE("dry hops add no IBU") {
        const HopAddition hops[] = {one_ounce(0.0, HopUse::DryHop)};
        CHECK(total_ibu(hops, boil) == 0.0);
    }
    SUBCASE("first-wort hops boil for the whole boil, times their factor") {
        const HopAddition hops[] = {one_ounce(0.0, HopUse::FirstWort)};
        IbuSettings settings;
        settings.first_wort_factor = 1.1;
        CHECK(total_ibu(hops, boil, settings) == near(1.1 * full_boil_ibu, 1e-9));
    }
    SUBCASE("mash hops are a fraction of a full-length boil addition") {
        const HopAddition hops[] = {one_ounce(0.0, HopUse::Mash)};
        CHECK(total_ibu(hops, boil) == near(0.2 * full_boil_ibu, 1e-9));
    }
    SUBCASE("whirlpool hops scale the steep-time utilization by temperature") {
        HopAddition hop = one_ounce(20.0, HopUse::Whirlpool);
        hop.temperature_c = 80.0;
        const HopAddition whirlpool[] = {hop};
        const HopAddition boil_20[] = {one_ounce(20.0)};
        CHECK(total_ibu(whirlpool, boil) == near(0.228993 * total_ibu(boil_20, boil), 1e-4));
    }
}

TEST_CASE("per-addition IBUs sum to the total, and settings scale them") {
    const HopAddition hops[] = {one_ounce(60.0), one_ounce(15.0), one_ounce(0.0, HopUse::DryHop)};
    const BoilConditions boil = five_gallons();

    const std::vector<double> ibus = addition_ibus(hops, boil);
    REQUIRE(ibus.size() == 3);
    CHECK(ibus[0] + ibus[1] + ibus[2] == near(total_ibu(hops, boil), 1e-12));
    CHECK(ibus[0] > ibus[1]);

    IbuSettings scaled;
    scaled.utilization_scale = 0.9;
    scaled.form_factor[static_cast<std::size_t>(HopForm::Pellet)] = 1.1;
    CHECK(total_ibu(hops, boil, scaled) == near(0.99 * total_ibu(hops, boil), 1e-9));
}

TEST_CASE("impossible inputs give NaN") {
    const HopAddition hops[] = {one_ounce(60.0)};
    BoilConditions no_volume = five_gallons();
    no_volume.volume_l = 0.0;
    CHECK(std::isnan(total_ibu(hops, no_volume)));

    HopAddition negative = one_ounce(60.0);
    negative.mass_kg = -0.01;
    const HopAddition bad[] = {negative};
    CHECK(std::isnan(total_ibu(bad, five_gallons())));
}
