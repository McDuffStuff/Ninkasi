#pragma once
// Shared helpers for the unit tests.

#include <doctest/doctest.h>

// Matches values within ±tolerance of `expected`. doctest's Approx is relative
// by default; brewing reference values are usually stated to a fixed number
// of decimals, so an absolute tolerance reads more naturally.
//   CHECK(sg_to_plato(1.040) == near(10.0, 0.01));
inline doctest::Approx near(double expected, double tolerance) {
    constexpr double kScale = 1.0e9;
    return doctest::Approx(expected).scale(kScale).epsilon(tolerance / kScale);
}
