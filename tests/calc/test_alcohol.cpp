#include "calc/alcohol.h"
#include "test_helpers.h"

using namespace ninkasi::calc;

// OG 1.050 → FG 1.010 throughout, with the published formulas evaluated
// independently: OE 12.388 °P, AE 2.561 °P, RE 4.337 °P.

TEST_CASE("ABV formulas") {
    CHECK(abv(1.050, 1.010, AbvFormula::Simple) == near(0.0525, 1e-12));
    CHECK(abv(1.050, 1.010, AbvFormula::Alternate) == near(0.0533941, 1e-6));
    CHECK(abv(1.050, 1.010, AbvFormula::Balling) == near(0.0531610, 1e-6));
    CHECK(abw(1.050, 1.010) == near(0.0416162, 1e-6));
    CHECK(real_extract_plato(1.050, 1.010) == near(4.33748, 1e-4));
}

TEST_CASE("attenuation") {
    CHECK(apparent_attenuation(1.050, 1.010) == near(0.793280, 1e-5));
    CHECK(real_attenuation(1.050, 1.010) == near(0.649855, 1e-5));
    // Balling's real extract makes real attenuation exactly 0.8192 × apparent.
    CHECK(real_attenuation(1.072, 1.016) == near(0.8192 * apparent_attenuation(1.072, 1.016), 1e-12));
}

TEST_CASE("predicted FG round-trips through attenuation") {
    const double fg = predict_fg(1.060, 0.78);
    CHECK(apparent_attenuation(1.060, fg) == near(0.78, 1e-9));
    CHECK(fg == near(1.0132, 0.001));  // the gravity-points estimate
}
