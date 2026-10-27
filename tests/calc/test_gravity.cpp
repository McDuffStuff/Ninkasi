#include "calc/gravity.h"
#include "test_helpers.h"

#include <cmath>

using namespace ninkasi::calc;

TEST_CASE("SG to Plato matches the sucrose tables") {
    // USDA sucrose conversion table: 10.3 °Bx is SG 1.04127 (20/20 °C).
    CHECK(sg_to_plato(1.04127) == near(10.30, 0.005));

    // An independent fit to the NBS sucrose table (Wikipedia, "Brix").
    auto nbs_fit = [](double sg) {
        return 182.46007 * sg * sg * sg - 775.68212 * sg * sg + 1262.7794 * sg - 669.56218;
    };
    for (double sg : {1.020, 1.040, 1.060, 1.080, 1.100}) {
        CAPTURE(sg);
        CHECK(sg_to_plato(sg) == near(nbs_fit(sg), 0.01));
    }
}

TEST_CASE("plato_to_sg is the exact inverse of sg_to_plato") {
    for (double plato : {-2.0, 0.0, 5.0, 12.0, 20.0, 30.0}) {
        CAPTURE(plato);
        CHECK(sg_to_plato(plato_to_sg(plato)) == near(plato, 1e-9));
    }
    CHECK(plato_to_sg(12.0) == near(1.048, 0.0005));  // brewer's rule: 12 °P ≈ 1.048
    CHECK(brix_to_sg(12.0) == plato_to_sg(12.0));
    CHECK(gravity_points(1.050) == near(50.0, 1e-9));
}

TEST_CASE("hydrometer temperature correction") {
    // brewery.org correction table for a hydrometer calibrated at 59 °F (15 °C):
    // +0.90 points at 68 °F, +6.14 at 100.4 °F, +15.89 at 140 °F.
    CHECK(hydrometer_corrected_sg(1.000, 20.0, 15.0) == near(1.00090, 0.0003));
    CHECK(hydrometer_corrected_sg(1.000, 38.0, 15.0) == near(1.00614, 0.0003));
    CHECK(hydrometer_corrected_sg(1.000, 60.0, 15.0) == near(1.01589, 0.0003));

    // No correction at the calibration temperature; cold samples read high.
    CHECK(hydrometer_corrected_sg(1.052, 20.0, 20.0) == near(1.052, 1e-12));
    CHECK(hydrometer_corrected_sg(1.050, 5.0, 20.0) < 1.050);
}

TEST_CASE("refractometer readings of unfermented wort") {
    // 12.48 °Bx on the refractometer with a WCF of 1.04 is 12.0 °P.
    CHECK(refractometer_wort_sg(12.48, 1.04) == near(plato_to_sg(12.0), 1e-12));
    CHECK(wort_correction_factor(12.48, plato_to_sg(12.0)) == near(1.04, 1e-9));
    CHECK(std::isnan(refractometer_wort_sg(12.0, 0.0)));
}

TEST_CASE("refractometer readings during fermentation use Terrill's cubic") {
    // The published cubic evaluated independently: 12.0 → 6.0 °Bx, WCF 1.04.
    CHECK(refractometer_fermenting_sg(12.0, 6.0, 1.04) == near(1.0113752, 1e-6));
    // Ignoring the alcohol would badly overstate the gravity.
    CHECK(refractometer_fermenting_sg(12.0, 6.0, 1.04) < refractometer_wort_sg(6.0, 1.04) - 0.010);
}
