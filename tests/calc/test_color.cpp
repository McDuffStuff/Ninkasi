#include "calc/color.h"
#include "calc/units.h"
#include "test_helpers.h"

#include <cmath>

using namespace ninkasi::calc;

TEST_CASE("malt color units and Morey") {
    // 10 lb of 2 °L base malt and 1 lb of 60 °L crystal in 5 gal:
    // MCU = (10 × 2 + 1 × 60) / 5 = 16; Morey gives 9.99 SRM.
    const GrainColor grist[] = {
        {.mass_kg = 10.0 * kKilogramsPerPound, .color_lovibond = 2.0},
        {.mass_kg = 1.0 * kKilogramsPerPound, .color_lovibond = 60.0},
    };
    const double volume_l = 5.0 * kLitersPerUsGallon;
    CHECK(malt_color_units(grist, volume_l) == near(16.0, 1e-9));
    CHECK(estimate_srm(grist, volume_l) == near(9.99385, 1e-4));
}

TEST_CASE("Daniels and Mosher") {
    CHECK(srm_from_mcu(16.0, ColorModel::Daniels) == near(11.6, 1e-12));
    CHECK(srm_from_mcu(16.0, ColorModel::Mosher) == near(9.5, 1e-12));
    // Pale beers: the straight-line fits would add color that isn't there.
    CHECK(srm_from_mcu(3.0, ColorModel::Daniels) == near(3.0, 1e-12));
    CHECK(srm_from_mcu(3.0, ColorModel::Mosher) == near(3.0, 1e-12));
}

TEST_CASE("EBC is 1.97 times SRM") {
    CHECK(srm_to_ebc(10.0) == near(19.7, 1e-12));
    CHECK(ebc_to_srm(19.7) == near(10.0, 1e-12));
}

TEST_CASE("impossible inputs give NaN") {
    const GrainColor grist[] = {{.mass_kg = 1.0, .color_lovibond = 2.0}};
    CHECK(std::isnan(malt_color_units(grist, 0.0)));
    CHECK(std::isnan(srm_from_mcu(-1.0, ColorModel::Morey)));
}
