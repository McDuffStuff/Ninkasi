#pragma once
// Units of measure and conversions between them.
//
// Ninkasi stores and calculates everything in canonical units (see
// calc/common.h) and converts only when showing a value or reading one the
// brewer typed.
//
// Each Unit belongs to a physical Dimension (volume, mass...). The brewer
// chooses units per Measure, which is what is being measured: grain and hops
// are both masses, but brewers weigh grain in pounds and salts in grams. An
// element can override any of these choices.

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace ninkasi::calc {

// Exact by definition.
inline constexpr double kLitersPerUsGallon = 3.785411784;
inline constexpr double kLitersPerImperialGallon = 4.54609;
inline constexpr double kKilogramsPerPound = 0.45359237;

enum class Dimension {
    Volume,       // canonical: liter
    Mass,         // canonical: kilogram
    Temperature,  // canonical: degree Celsius
    Gravity,      // canonical: specific gravity
    Color,        // canonical: SRM
};

enum class Unit {
    // Volume
    Liter, Milliliter, Hectoliter, UsFluidOunce, UsQuart, UsGallon, UsBarrel, ImperialGallon, UkBarrel,
    // Mass
    Kilogram, Gram, Milligram, Ounce, Pound,
    // Temperature
    Celsius, Fahrenheit,
    // Gravity. Plato and Brix are treated as the same scale (see calc/gravity.h).
    SpecificGravity, Plato, Brix,
    // Color. °L is treated as equal to SRM, the convention of modern (ASBC)
    // malt analyses; EBC = 1.97 × SRM.
    Srm, Ebc, Lovibond,
};

// What a value measures, for choosing its unit.
enum class Measure {
    Volume,        // batches, vessels, water
    Mass,          // grain and other bulk ingredients
    HopMass,       // hop additions
    AdditionMass,  // salts, finings, nutrients and other small additions
    Temperature,
    Gravity,
    Color,         // beer color
    MaltColor,     // malt color
};

inline constexpr std::array kAllMeasures = {
    Measure::Volume,      Measure::Mass,    Measure::HopMass, Measure::AdditionMass,
    Measure::Temperature, Measure::Gravity, Measure::Color,   Measure::MaltColor,
};
inline constexpr std::size_t kMeasureCount = kAllMeasures.size();

// Position of a measure in per-measure arrays such as UnitOverrides.
inline constexpr std::size_t index_of(Measure measure) {
    return static_cast<std::size_t>(measure);
}

// --- Unit and measure information -------------------------------------------

Dimension dimension_of(Unit unit);
std::string_view symbol(Unit unit);  // "gal", "°P", ...
std::string_view name(Unit unit);    // "US gallon", ...
std::string_view key(Unit unit);     // stable identifier for saved settings, e.g. "us_gallon"
int display_decimals(Unit unit);
std::optional<Unit> unit_from_key(std::string_view unit_key);

// The units of a dimension, in menu order; the first is canonical.
std::span<const Unit> units_of(Dimension dimension);
Unit canonical_unit(Dimension dimension);

Dimension dimension_of(Measure measure);
std::string_view name(Measure measure);  // "Hop mass", ...
std::string_view key(Measure measure);   // stable identifier for saved settings, e.g. "hop_mass"
std::optional<Measure> measure_from_key(std::string_view measure_key);
// The units a measure can be shown in.
std::span<const Unit> units_of(Measure measure);

// --- Conversion -------------------------------------------------------------

double to_canonical(double value, Unit from);
double from_canonical(double canonical_value, Unit to);
// Converts between two units of the same dimension; kInvalid otherwise.
double convert(double value, Unit from, Unit to);

// Formats a canonical value in `unit` at its display precision, e.g.
// format(117.35, Unit::UsBarrel) == "1.00 bbl". Invalid values show as "--".
std::string format(double canonical_value, Unit unit, bool with_symbol = true);

// --- Preferences ------------------------------------------------------------

// Unit choices for one element, indexed by index_of(Measure). An empty entry
// follows the brewery-wide preference.
using UnitOverrides = std::array<std::optional<Unit>, kMeasureCount>;

// The unit the brewer has chosen for each measure.
class UnitPreferences {
public:
    // Starts with every measure in its dimension's canonical (metric) unit.
    UnitPreferences();

    // Starting points: barrels, pounds, grams for small additions, °F, °P,
    // SRM and °L ...
    static UnitPreferences us_brewery();
    // ... or hectoliters, kilograms, grams for small additions, °C, °P and EBC.
    static UnitPreferences metric_brewery();

    Unit unit_for(Measure measure) const;
    // Chooses `unit` for `measure`; ignored if the unit can't measure it.
    void set(Measure measure, Unit unit);

    // These preferences with an element's overrides applied on top.
    UnitPreferences with_overrides(const UnitOverrides& overrides) const;

    bool operator==(const UnitPreferences&) const = default;

private:
    std::array<Unit, kMeasureCount> units_{};
};

} // namespace ninkasi::calc
