#include "domain/activity.h"
#include "test_helpers.h"

using namespace ninkasi;
using domain::render_quantities;

TEST_CASE("activity text shows quantities in the reader's units") {
    const std::string text = "Moved " + domain::volume_token(1173.48) + " at " + domain::gravity_token(1.012) +
                             ", " + domain::temperature_token(20.0);

    calc::UnitPreferences us = calc::UnitPreferences::us_brewery();
    us.set(calc::Measure::Gravity, calc::Unit::SpecificGravity);
    CHECK(render_quantities(text, us) == "Moved 10.00 bbl at 1.012 SG, 68.0 °F");

    calc::UnitPreferences metric = calc::UnitPreferences::metric_brewery();
    metric.set(calc::Measure::Gravity, calc::Unit::SpecificGravity);
    CHECK(render_quantities(text, metric) == "Moved 11.73 hL at 1.012 SG, 20.0 °C");
}

TEST_CASE("text that isn't a token is left alone") {
    const calc::UnitPreferences units;
    CHECK(render_quantities("plain text", units) == "plain text");
    CHECK(render_quantities("{not a token}", units) == "{not a token}");
    CHECK(render_quantities("{volume:abc}", units) == "{volume:abc}");
    CHECK(render_quantities("{mass:5}", units) == "{mass:5}");  // unknown kind
    CHECK(render_quantities("unclosed {volume:5", units) == "unclosed {volume:5");
    CHECK(render_quantities("{", units) == "{");
}
