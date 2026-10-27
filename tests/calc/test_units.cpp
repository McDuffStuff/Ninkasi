#include "calc/common.h"
#include "calc/gravity.h"
#include "calc/units.h"
#include "test_helpers.h"

#include <cmath>
#include <string>

using namespace ninkasi::calc;

TEST_CASE("near() enforces its tolerance") {
    // Every calc test relies on this helper; make sure it can fail.
    CHECK(1.000 == near(1.005, 0.01));
    CHECK(1.000 != near(1.020, 0.01));
    CHECK(1000.0 != near(1000.5, 0.1));  // absolute, not relative
}

TEST_CASE("units match their legal definitions") {
    CHECK(to_canonical(1.0, Unit::UsGallon) == near(3.785411784, 1e-12));
    CHECK(to_canonical(1.0, Unit::UsBarrel) == near(117.347765304, 1e-9));  // 31 US gal
    CHECK(to_canonical(128.0, Unit::UsFluidOunce) == near(3.785411784, 1e-12));
    CHECK(to_canonical(4.0, Unit::UsQuart) == near(3.785411784, 1e-12));
    CHECK(to_canonical(1.0, Unit::Hectoliter) == near(100.0, 1e-12));
    CHECK(to_canonical(1.0, Unit::UkBarrel) == near(163.65924, 1e-9));     // 36 imperial gal
    CHECK(to_canonical(1.0, Unit::Pound) == near(0.45359237, 1e-12));
    CHECK(to_canonical(16.0, Unit::Ounce) == near(0.45359237, 1e-12));
    CHECK(to_canonical(1000.0, Unit::Gram) == near(1.0, 1e-12));
}

TEST_CASE("temperature conversions") {
    CHECK(to_canonical(212.0, Unit::Fahrenheit) == near(100.0, 1e-9));
    CHECK(to_canonical(32.0, Unit::Fahrenheit) == near(0.0, 1e-9));
    CHECK(to_canonical(-40.0, Unit::Fahrenheit) == near(-40.0, 1e-9));
    CHECK(from_canonical(20.0, Unit::Fahrenheit) == near(68.0, 1e-9));
}

TEST_CASE("convert works within a dimension only") {
    CHECK(convert(31.0, Unit::UsGallon, Unit::UsBarrel) == near(1.0, 1e-12));
    CHECK(convert(1.0, Unit::UsBarrel, Unit::Hectoliter) == near(1.17347765304, 1e-12));
    CHECK(convert(19.7, Unit::Ebc, Unit::Srm) == near(10.0, 1e-12));
    CHECK(convert(60.0, Unit::Lovibond, Unit::MaltEbc) == near(118.2, 1e-9));
    CHECK(std::isnan(convert(1.0, Unit::Liter, Unit::Kilogram)));
}

TEST_CASE("gravity units convert on the Plato scale") {
    CHECK(to_canonical(12.0, Unit::Plato) == near(plato_to_sg(12.0), 1e-12));
    CHECK(from_canonical(1.050, Unit::Plato) == near(sg_to_plato(1.050), 1e-12));
    CHECK(from_canonical(1.050, Unit::Brix) == near(sg_to_plato(1.050), 1e-12));
}

TEST_CASE("every unit is consistent with its dimension") {
    for (Dimension dimension : kAllDimensions) {
        const std::string dimension_name(name(dimension));
        CAPTURE(dimension_name);
        REQUIRE_FALSE(units_of(dimension).empty());

        // The canonical unit converts to itself unchanged.
        CHECK(to_canonical(7.5, canonical_unit(dimension)) == near(7.5, 1e-12));
        CHECK(dimension_from_key(key(dimension)) == dimension);

        for (Unit unit : units_of(dimension)) {
            const std::string unit_key(key(unit));
            CAPTURE(unit_key);
            CHECK(dimension_of(unit) == dimension);
            CHECK(unit_from_key(key(unit)) == unit);  // keys are unique and round-trip
            CHECK_FALSE(symbol(unit).empty());
            CHECK(to_canonical(from_canonical(1.05, unit), unit) == near(1.05, 1e-9));
        }
    }
    CHECK_FALSE(unit_from_key("furlong").has_value());
    CHECK_FALSE(dimension_from_key("luminosity").has_value());
}

TEST_CASE("format shows a canonical value in the chosen unit") {
    CHECK(format(1.0504, Unit::SpecificGravity) == "1.050 SG");
    CHECK(format(plato_to_sg(12.0), Unit::Plato) == "12.0 °P");
    CHECK(format(117.347765304, Unit::UsBarrel) == "1.00 bbl");
    CHECK(format(100.0, Unit::Fahrenheit, false) == "212.0");
    CHECK(format(-0.01, Unit::Celsius) == "0.0 °C");  // never "-0.0"
    CHECK(format(kInvalid, Unit::Liter) == "--");
}

TEST_CASE("unit preferences") {
    const UnitPreferences defaults;
    for (Dimension dimension : kAllDimensions) {
        CHECK(defaults.unit_for(dimension) == canonical_unit(dimension));
    }

    const UnitPreferences us = UnitPreferences::us_brewery();
    CHECK(us.unit_for(Dimension::Volume) == Unit::UsBarrel);
    CHECK(us.unit_for(Dimension::Mass) == Unit::Pound);
    CHECK(us.unit_for(Dimension::Temperature) == Unit::Fahrenheit);
    CHECK(us.unit_for(Dimension::Gravity) == Unit::Plato);
    CHECK(us.unit_for(Dimension::Color) == Unit::Srm);
    CHECK(us.unit_for(Dimension::MaltColor) == Unit::Lovibond);

    const UnitPreferences metric = UnitPreferences::metric_brewery();
    CHECK(metric.unit_for(Dimension::Volume) == Unit::Hectoliter);
    CHECK(metric.unit_for(Dimension::MaltColor) == Unit::MaltEbc);

    // Choosing a unit changes only its own dimension.
    UnitPreferences changed = us;
    changed.set(Unit::UsGallon);
    CHECK(changed.unit_for(Dimension::Volume) == Unit::UsGallon);
    CHECK(changed.unit_for(Dimension::Mass) == Unit::Pound);
    CHECK(changed != us);
}

TEST_CASE("element overrides replace only the dimensions they set") {
    const UnitPreferences us = UnitPreferences::us_brewery();
    UnitOverrides overrides{};
    overrides[index_of(Dimension::Volume)] = Unit::UsGallon;
    overrides[index_of(Dimension::Mass)] = Unit::Celsius;  // filed under the wrong dimension: ignored

    const UnitPreferences effective = us.with_overrides(overrides);
    CHECK(effective.unit_for(Dimension::Volume) == Unit::UsGallon);
    CHECK(effective.unit_for(Dimension::Mass) == Unit::Pound);
    CHECK(effective.unit_for(Dimension::Temperature) == Unit::Fahrenheit);
    CHECK(us.with_overrides(UnitOverrides{}) == us);
}
