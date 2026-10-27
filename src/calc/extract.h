#pragma once
// Extract (the dissolved sugars in wort) and brewhouse efficiency.
//
// Gravity is predicted from the *mass* of extract, the way professional
// brewers work: extract = wort mass × °P / 100. This holds at any gravity, so
// no "points per pound" constant is needed. A fermentable's yield is the
// fraction of its mass that can become extract.

#include <span>

namespace ninkasi::calc {

// Mass of extract (kg) dissolved in `volume_l` of wort at `sg`.
// Use the cooled volume: hot wort is ~4 % larger.
double extract_kg(double volume_l, double sg);

// Gravity of `volume_l` of wort that contains `extract_mass_kg` of extract.
double sg_from_extract(double extract_mass_kg, double volume_l);

// The parts of a fermentable that matter for gravity.
struct Fermentable {
    double mass_kg = 0.0;
    double yield = 0.0;   // as-is extract yield, e.g. 0.80 for typical 2-row
    bool mashed = true;   // false for sugars and extracts added to the kettle
};

// Extract (kg) if every fermentable gave up its full yield.
double potential_extract_kg(std::span<const Fermentable> fermentables);

// Extract (kg) expected when mashed fermentables are extracted at
// `mash_efficiency`; sugars and extracts added to the kettle count in full.
double expected_extract_kg(std::span<const Fermentable> fermentables, double mash_efficiency);

// Fraction of the potential extract actually recovered.
double efficiency(double actual_kg, double potential_kg);

// --- Malt specifications ----------------------------------------------------

// As-is yield from a malt analysis sheet:
//   (fine-grind dry-basis extract − fine/coarse difference) × (1 − moisture).
// Leave the difference at 0 for a fine-grind yield.
double as_is_yield(double dry_basis_extract, double moisture, double fine_coarse_difference = 0.0);

// "Potential" gravity, the homebrew way of stating yield: the SG of 1 lb of
// the fermentable dissolved to make 1 US gallon (sucrose ≈ 1.046, i.e.
// 46 points per pound per gallon). Both directions use the extract model
// above, so they agree with Ninkasi's gravity predictions.
double potential_sg_from_yield(double yield);
double yield_from_potential_sg(double potential_sg);

} // namespace ninkasi::calc
