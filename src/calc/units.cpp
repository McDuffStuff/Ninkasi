#include "calc/units.h"

#include "calc/color.h"
#include "calc/common.h"
#include "calc/gravity.h"

#include <cstdio>
#include <iterator>

namespace ninkasi::calc {

namespace {

// How a unit relates to its dimension's canonical unit.
enum class Conversion {
    Linear,      // canonical = value × factor + offset
    PlatoScale,  // non-linear: converted with sg_to_plato / plato_to_sg
};

struct UnitRow {
    Unit unit;
    Dimension dimension;
    std::string_view key;
    std::string_view symbol;
    std::string_view name;
    int decimals;
    Conversion conversion;
    double factor;
    double offset;
};

using enum Conversion;

// One row per Unit, in enum order, grouped by dimension with the canonical
// unit first. The static_asserts below enforce this.
constexpr UnitRow kUnits[] = {
    {Unit::Liter,           Dimension::Volume,      "liter",            "L",       "Liter",            1, Linear,     1.0, 0.0},
    {Unit::Milliliter,      Dimension::Volume,      "milliliter",       "mL",      "Milliliter",       0, Linear,     0.001, 0.0},
    {Unit::Hectoliter,      Dimension::Volume,      "hectoliter",       "hL",      "Hectoliter",       2, Linear,     100.0, 0.0},
    {Unit::UsFluidOunce,    Dimension::Volume,      "us_fluid_ounce",   "fl oz",   "US fluid ounce",   1, Linear,     kLitersPerUsGallon / 128.0, 0.0},
    {Unit::UsQuart,         Dimension::Volume,      "us_quart",         "qt",      "US quart",         2, Linear,     kLitersPerUsGallon / 4.0, 0.0},
    {Unit::UsGallon,        Dimension::Volume,      "us_gallon",        "gal",     "US gallon",        2, Linear,     kLitersPerUsGallon, 0.0},
    {Unit::UsBarrel,        Dimension::Volume,      "us_barrel",        "bbl",     "US beer barrel",   2, Linear,     31.0 * kLitersPerUsGallon, 0.0},
    {Unit::ImperialGallon,  Dimension::Volume,      "imperial_gallon",  "imp gal", "Imperial gallon",  2, Linear,     kLitersPerImperialGallon, 0.0},
    {Unit::UkBarrel,        Dimension::Volume,      "uk_barrel",        "UK bbl",  "UK beer barrel",   2, Linear,     36.0 * kLitersPerImperialGallon, 0.0},

    {Unit::Kilogram,        Dimension::Mass,        "kilogram",         "kg",      "Kilogram",         2, Linear,     1.0, 0.0},
    {Unit::Gram,            Dimension::Mass,        "gram",             "g",       "Gram",             1, Linear,     0.001, 0.0},
    {Unit::Milligram,       Dimension::Mass,        "milligram",        "mg",      "Milligram",        0, Linear,     1.0e-6, 0.0},
    {Unit::Ounce,           Dimension::Mass,        "ounce",            "oz",      "Ounce",            2, Linear,     kKilogramsPerPound / 16.0, 0.0},
    {Unit::Pound,           Dimension::Mass,        "pound",            "lb",      "Pound",            2, Linear,     kKilogramsPerPound, 0.0},

    {Unit::Celsius,         Dimension::Temperature, "celsius",          "°C",      "Celsius",          1, Linear,     1.0, 0.0},
    {Unit::Fahrenheit,      Dimension::Temperature, "fahrenheit",       "°F",      "Fahrenheit",       1, Linear,     5.0 / 9.0, -32.0 * 5.0 / 9.0},

    {Unit::SpecificGravity, Dimension::Gravity,     "specific_gravity", "SG",      "Specific gravity", 3, Linear,     1.0, 0.0},
    {Unit::Plato,           Dimension::Gravity,     "plato",            "°P",      "Degrees Plato",    1, PlatoScale, 0.0, 0.0},
    {Unit::Brix,            Dimension::Gravity,     "brix",             "°Bx",     "Degrees Brix",     1, PlatoScale, 0.0, 0.0},

    {Unit::Srm,             Dimension::Color,       "srm",              "SRM",     "SRM",              1, Linear,     1.0, 0.0},
    {Unit::Ebc,             Dimension::Color,       "ebc",              "EBC",     "EBC",              1, Linear,     1.0 / kEbcPerSrm, 0.0},

    {Unit::Lovibond,        Dimension::MaltColor,   "lovibond",         "°L",      "Degrees Lovibond", 1, Linear,     1.0, 0.0},
    {Unit::MaltEbc,         Dimension::MaltColor,   "malt_ebc",         "EBC",     "EBC",              1, Linear,     1.0 / kEbcPerSrm, 0.0},
    {Unit::MaltSrm,         Dimension::MaltColor,   "malt_srm",         "SRM",     "SRM",              1, Linear,     1.0, 0.0},
};

constexpr bool rows_match_enum_order() {
    for (std::size_t i = 0; i < std::size(kUnits); ++i) {
        if (static_cast<std::size_t>(kUnits[i].unit) != i) return false;
    }
    return true;
}
static_assert(rows_match_enum_order(), "kUnits must list units in enum Unit order");

constexpr bool dimensions_are_grouped() {
    for (std::size_t i = 1; i < std::size(kUnits); ++i) {
        if (kUnits[i].dimension == kUnits[i - 1].dimension) continue;
        // A new group starts at i; its dimension must not have appeared before.
        for (std::size_t j = 0; j < i; ++j) {
            if (kUnits[j].dimension == kUnits[i].dimension) return false;
        }
    }
    return true;
}
static_assert(dimensions_are_grouped(), "kUnits must keep each dimension's units together");

// Just the Unit column, so units_of() can hand out spans of it.
constexpr auto kAllUnits = [] {
    std::array<Unit, std::size(kUnits)> units{};
    for (std::size_t i = 0; i < units.size(); ++i) units[i] = kUnits[i].unit;
    return units;
}();

struct DimensionRow {
    Dimension dimension;
    std::string_view key;
    std::string_view name;
};

constexpr DimensionRow kDimensions[] = {
    {Dimension::Volume,      "volume",      "Volume"},
    {Dimension::Mass,        "mass",        "Mass"},
    {Dimension::Temperature, "temperature", "Temperature"},
    {Dimension::Gravity,     "gravity",     "Gravity"},
    {Dimension::Color,       "color",       "Beer color"},
    {Dimension::MaltColor,   "malt_color",  "Malt color"},
};
static_assert(std::size(kDimensions) == kDimensionCount);

const UnitRow& row(Unit unit) {
    return kUnits[static_cast<std::size_t>(unit)];
}

const DimensionRow& row(Dimension dimension) {
    return kDimensions[index_of(dimension)];
}

} // namespace

// --- Unit and dimension information ---------------------------------------

Dimension dimension_of(Unit unit) { return row(unit).dimension; }
std::string_view symbol(Unit unit) { return row(unit).symbol; }
std::string_view name(Unit unit) { return row(unit).name; }
std::string_view key(Unit unit) { return row(unit).key; }
int display_decimals(Unit unit) { return row(unit).decimals; }

std::optional<Unit> unit_from_key(std::string_view unit_key) {
    for (const UnitRow& r : kUnits) {
        if (r.key == unit_key) return r.unit;
    }
    return std::nullopt;
}

std::string_view name(Dimension dimension) { return row(dimension).name; }
std::string_view key(Dimension dimension) { return row(dimension).key; }

std::optional<Dimension> dimension_from_key(std::string_view dimension_key) {
    for (const DimensionRow& r : kDimensions) {
        if (r.key == dimension_key) return r.dimension;
    }
    return std::nullopt;
}

std::span<const Unit> units_of(Dimension dimension) {
    std::size_t first = 0;
    while (first < kAllUnits.size() && kUnits[first].dimension != dimension) ++first;
    std::size_t last = first;
    while (last < kAllUnits.size() && kUnits[last].dimension == dimension) ++last;
    return std::span<const Unit>(kAllUnits).subspan(first, last - first);
}

Unit canonical_unit(Dimension dimension) {
    return units_of(dimension).front();
}

// --- Conversion -------------------------------------------------------------

double to_canonical(double value, Unit from) {
    const UnitRow& r = row(from);
    if (r.conversion == PlatoScale) return plato_to_sg(value);
    return value * r.factor + r.offset;
}

double from_canonical(double canonical_value, Unit to) {
    const UnitRow& r = row(to);
    if (r.conversion == PlatoScale) return sg_to_plato(canonical_value);
    return (canonical_value - r.offset) / r.factor;
}

double convert(double value, Unit from, Unit to) {
    if (dimension_of(from) != dimension_of(to)) return kInvalid;
    return from_canonical(to_canonical(value, from), to);
}

std::string format(double canonical_value, Unit unit, bool with_symbol) {
    double value = from_canonical(canonical_value, unit);
    if (!is_valid(value)) return "--";

    const int decimals = row(unit).decimals;
    // Values that round to zero print as "0.0", never "-0.0".
    if (std::abs(value) < 0.5 * std::pow(10.0, -decimals)) value = 0.0;

    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%.*f", decimals, value);
    std::string text(buffer);
    if (with_symbol) {
        text += ' ';
        text += row(unit).symbol;
    }
    return text;
}

// --- Preferences ------------------------------------------------------------

UnitPreferences::UnitPreferences() {
    for (Dimension dimension : kAllDimensions) {
        units_[index_of(dimension)] = canonical_unit(dimension);
    }
}

UnitPreferences UnitPreferences::us_brewery() {
    UnitPreferences prefs;
    for (Unit unit : {Unit::UsBarrel, Unit::Pound, Unit::Fahrenheit, Unit::Plato, Unit::Srm, Unit::Lovibond}) {
        prefs.set(unit);
    }
    return prefs;
}

UnitPreferences UnitPreferences::metric_brewery() {
    UnitPreferences prefs;
    for (Unit unit : {Unit::Hectoliter, Unit::Kilogram, Unit::Celsius, Unit::Plato, Unit::Ebc, Unit::MaltEbc}) {
        prefs.set(unit);
    }
    return prefs;
}

Unit UnitPreferences::unit_for(Dimension dimension) const {
    return units_[index_of(dimension)];
}

void UnitPreferences::set(Unit unit) {
    units_[index_of(dimension_of(unit))] = unit;
}

UnitPreferences UnitPreferences::with_overrides(const UnitOverrides& overrides) const {
    UnitPreferences effective = *this;
    for (Dimension dimension : kAllDimensions) {
        const std::optional<Unit>& unit = overrides[index_of(dimension)];
        // Only honor an override that measures the dimension it is filed under.
        if (unit && dimension_of(*unit) == dimension) effective.set(*unit);
    }
    return effective;
}

} // namespace ninkasi::calc
