#pragma once
// Units of measure and conversions between them.
//
// Ninkasi stores and calculates everything in canonical units (see
// calc/common.h) and converts only when showing a value or reading one the
// brewer typed. Each unit belongs to one Dimension; the brewer picks a unit
// per dimension (UnitPreferences), and an element can override that choice.

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
    Color,        // beer color, canonical: SRM
    MaltColor,    // malt color, canonical: degree Lovibond
};

inline constexpr std::array kAllDimensions = {
    Dimension::Volume, Dimension::Mass,  Dimension::Temperature,
    Dimension::Gravity, Dimension::Color, Dimension::MaltColor,
};
inline constexpr std::size_t kDimensionCount = kAllDimensions.size();

// Position of a dimension in per-dimension arrays such as UnitOverrides.
inline constexpr std::size_t index_of(Dimension dimension) {
    return static_cast<std::size_t>(dimension);
}

enum class Unit {
    // Volume
    Liter, Milliliter, Hectoliter, UsFluidOunce, UsQuart, UsGallon, UsBarrel, ImperialGallon, UkBarrel,
    // Mass
    Kilogram, Gram, Milligram, Ounce, Pound,
    // Temperature
    Celsius, Fahrenheit,
    // Gravity. Plato and Brix are treated as the same scale (see calc/gravity.h).
    SpecificGravity, Plato, Brix,
    // Beer color
    Srm, Ebc,
    // Malt color. °L and SRM are treated as equal for malt, the convention of
    // modern (ASBC) malt analyses; EBC = 1.97 × SRM.
    Lovibond, MaltEbc, MaltSrm,
};

// --- Unit and dimension information ---------------------------------------

Dimension dimension_of(Unit unit);
std::string_view symbol(Unit unit);  // "gal", "°P", ...
std::string_view name(Unit unit);    // "US gallon", ...
std::string_view key(Unit unit);     // stable identifier for saved settings, e.g. "us_gallon"
int display_decimals(Unit unit);
std::optional<Unit> unit_from_key(std::string_view unit_key);

std::string_view name(Dimension dimension);  // "Volume", "Malt color", ...
std::string_view key(Dimension dimension);   // "volume", "malt_color", ...
std::optional<Dimension> dimension_from_key(std::string_view dimension_key);

// The units that measure a dimension, in menu order; the first is canonical.
std::span<const Unit> units_of(Dimension dimension);
Unit canonical_unit(Dimension dimension);

// --- Conversion -------------------------------------------------------------

double to_canonical(double value, Unit from);
double from_canonical(double canonical_value, Unit to);
// Converts between two units of the same dimension; kInvalid otherwise.
double convert(double value, Unit from, Unit to);

// Formats a canonical value in `unit` at its display precision, e.g.
// format(117.35, Unit::UsBarrel) == "1.00 bbl". Invalid values show as "--".
std::string format(double canonical_value, Unit unit, bool with_symbol = true);

// --- Preferences ------------------------------------------------------------

// Unit choices for one element, indexed by index_of(Dimension). An empty
// entry follows the brewery-wide preference.
using UnitOverrides = std::array<std::optional<Unit>, kDimensionCount>;

// The unit the brewer has chosen for each dimension.
class UnitPreferences {
public:
    // Starts with every dimension in its canonical (metric) unit.
    UnitPreferences();

    // Starting points: barrels, pounds, °F, °P, SRM and °L ...
    static UnitPreferences us_brewery();
    // ... or hectoliters, kilograms, °C, °P and EBC.
    static UnitPreferences metric_brewery();

    Unit unit_for(Dimension dimension) const;
    // Chooses `unit` for the dimension it belongs to.
    void set(Unit unit);

    // These preferences with an element's overrides applied on top.
    UnitPreferences with_overrides(const UnitOverrides& overrides) const;

    bool operator==(const UnitPreferences&) const = default;

private:
    std::array<Unit, kDimensionCount> units_{};
};

} // namespace ninkasi::calc
