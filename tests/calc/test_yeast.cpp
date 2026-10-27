#include "calc/extract.h"
#include "calc/gravity.h"
#include "calc/yeast.h"
#include "test_helpers.h"

#include <cmath>

using namespace ninkasi::calc;

TEST_CASE("cells needed follow the pitch rate") {
    // 0.75 million/mL/°P × 20 000 mL × 12 °P = 180 billion cells.
    CHECK(cells_needed_billion(20.0, plato_to_sg(12.0), kAlePitchRate) == near(180.0, 1e-6));
    CHECK(cells_needed_billion(20.0, plato_to_sg(12.0), kLagerPitchRate) == near(360.0, 1e-6));
    CHECK(std::isnan(cells_needed_billion(-1.0, 1.050, kAlePitchRate)));
}

TEST_CASE("liquid yeast loses about 0.7 % viability a day") {
    CHECK(liquid_yeast_viability(0.0) == near(1.0, 1e-12));
    CHECK(liquid_yeast_viability(30.0) == near(0.79, 1e-12));
    CHECK(liquid_yeast_viability(200.0) == 0.0);
}

TEST_CASE("stir plate starters follow the Braukaiser model") {
    const double extract_g = extract_kg(1.0, 1.036) * 1000.0;  // ≈ 93 g in a 1 L, 1.036 starter
    CHECK(extract_g == near(93.0, 1.0));

    // Under 1.4 billion cells per gram: 1.4 billion new cells per gram.
    CHECK(starter_cells_billion(100.0, 1.0, 1.036, StarterMethod::StirPlate) == near(100.0 + 1.4 * extract_g, 1e-9));
    // Between 1.4 and 3.5: 2.33 − 0.67 × (cells per gram).
    const double crowded = 200.0 / extract_g;
    CHECK(starter_cells_billion(200.0, 1.0, 1.036, StarterMethod::StirPlate) ==
          near(200.0 + (2.33 - 0.67 * crowded) * extract_g, 1e-9));
    // Over 3.5: no growth.
    CHECK(starter_cells_billion(400.0, 1.0, 1.036, StarterMethod::StirPlate) == near(400.0, 1e-9));
}

TEST_CASE("unstirred starters follow the White model") {
    // 100 billion cells in 2 L: 50 million/mL, growth factor 1.0798 (evaluated independently).
    CHECK(starter_cells_billion(100.0, 2.0, 1.036, StarterMethod::NoAgitation) == near(207.981, 1e-3));
    CHECK(starter_cells_billion(100.0, 2.0, 1.036, StarterMethod::Shaken) == near(257.981, 1e-3));
    // Growth is capped at 6× new cells...
    CHECK(starter_cells_billion(1.0, 2.0, 1.036, StarterMethod::NoAgitation) == near(7.0, 1e-9));
    // ...and an overcrowded starter doesn't shrink.
    CHECK(starter_cells_billion(300.0, 1.0, 1.036, StarterMethod::NoAgitation) == near(300.0, 1e-9));
    CHECK(std::isnan(starter_cells_billion(100.0, 0.0, 1.036, StarterMethod::StirPlate)));
}
