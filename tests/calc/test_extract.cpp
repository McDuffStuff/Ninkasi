#include "calc/extract.h"
#include "calc/gravity.h"
#include "test_helpers.h"

#include <cmath>

using namespace ninkasi::calc;

TEST_CASE("extract mass and gravity are inverses") {
    for (double sg : {1.010, 1.030, 1.050, 1.080, 1.110}) {
        CAPTURE(sg);
        CHECK(sg_from_extract(extract_kg(20.0, sg), 20.0) == near(sg, 1e-10));
    }
}

TEST_CASE("extract agrees with the professional rule of thumb") {
    // kg of extract ≈ hL × °P × SG. The rule treats water as 1 kg/L; at 20 °C
    // it is 0.2 % lighter, so allow 0.3 %.
    const double sg = plato_to_sg(12.0);
    CHECK(extract_kg(1000.0, sg) == doctest::Approx(10.0 * 12.0 * sg).epsilon(0.003));
}

TEST_CASE("predicted gravity agrees with the points-per-pound method") {
    // 3.0 kg of extract in 23 L. Homebrew method: 6.614 lb × 46.2 / 6.076 gal ≈ 1.0503.
    CHECK(sg_from_extract(3.0, 23.0) == near(1.0503, 0.001));
}

TEST_CASE("potential gravity and yield") {
    // 1 lb of sucrose made up to 1 US gallon: published values 1.0461–1.0462.
    CHECK(potential_sg_from_yield(1.0) == near(1.0462, 0.0003));
    // 2-row at 37 points per pound per gallon is about 80 % yield.
    CHECK(yield_from_potential_sg(1.037) == near(0.80, 0.005));
    CHECK(potential_sg_from_yield(yield_from_potential_sg(1.037)) == near(1.037, 1e-10));
}

TEST_CASE("as-is yield from a malt analysis") {
    // 81 % fine-grind dry basis, 4 % moisture, 1.5 % fine/coarse difference.
    CHECK(as_is_yield(0.81, 0.04) == near(0.7776, 1e-12));
    CHECK(as_is_yield(0.81, 0.04, 0.015) == near(0.7632, 1e-12));
}

TEST_CASE("efficiency applies only to mashed fermentables") {
    const Fermentable grist[] = {
        {.mass_kg = 5.0, .yield = 0.80, .mashed = true},
        {.mass_kg = 0.5, .yield = 1.00, .mashed = false},  // sugar added to the kettle
    };
    CHECK(potential_extract_kg(grist) == near(4.5, 1e-12));
    CHECK(expected_extract_kg(grist, 0.75) == near(3.5, 1e-12));
    CHECK(efficiency(3.5, 4.5) == near(3.5 / 4.5, 1e-12));
}

TEST_CASE("impossible inputs give NaN") {
    CHECK(std::isnan(sg_from_extract(1.0, 0.0)));
    CHECK(std::isnan(sg_from_extract(-1.0, 10.0)));
    CHECK(std::isnan(efficiency(1.0, 0.0)));
    const Fermentable negative[] = {{.mass_kg = -1.0, .yield = 0.8}};
    CHECK(std::isnan(potential_extract_kg(negative)));
}
