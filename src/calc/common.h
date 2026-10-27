#pragma once
// Conventions shared by every calc module.
//
// Units: values are in Ninkasi's canonical units unless a parameter name says
// otherwise:
//   volume       liters             (_l)
//   mass         kilograms          (_kg)
//   temperature  degrees Celsius    (_c)
//   gravity      specific gravity   (_sg, or og / fg); extract in °Plato (_plato)
//   time         minutes            (_min)
// Ratios such as alpha acids, yields, efficiencies and ABV are fractions
// (0.055 means 5.5 %); only the UI shows them as percentages.
//
// Errors: calc functions never throw. When inputs are physically meaningless
// (a zero volume, a negative mass) they return kInvalid (NaN), which carries
// through any later math and is shown as "--" by the UI.

#include <cmath>
#include <limits>

namespace ninkasi::calc {

inline constexpr double kInvalid = std::numeric_limits<double>::quiet_NaN();

inline bool is_valid(double value) {
    return std::isfinite(value);
}

// Density of water at 20 °C in kg/L: the reference for SG measured at 20/20 °C.
inline constexpr double kWaterDensityKgPerL = 0.998203;

} // namespace ninkasi::calc
