#pragma once
// Every formula Ninkasi calculates with, written in LaTeX (calc/latex.h):
// what each one computes, from what, and where it comes from. The Formulas
// tab lists them.
//
// Most can be adjusted. A brewery's adjusted formula replaces the original
// wherever it's used: the calc functions ask adjusted_formula() first and
// otherwise run their built-in code, which the original LaTeX here matches
// (the tests check every one). A few are shown for reference only, because
// they add up a recipe's ingredients or are solved step by step; their parts
// are listed separately and can be adjusted.
//
// Adjusted formulas are the one piece of state in calc. Without any
// installed, every calculation is the original, pure function.

#include "calc/expression.h"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <string_view>

namespace ninkasi::calc {

enum class Formula : std::uint8_t {
    // Gravity
    SgToPlato, WaterDensity, HydrometerCorrection, RefractometerWort, RefractometerFermenting, WortCorrectionFactor,
    // Alcohol and attenuation
    AbvSimple, AbvAlternate, RealExtract, AlcoholByWeight, AbwToAbv, ApparentAttenuation, RealAttenuation,
    PredictedFinalExtract,
    // Bitterness
    TinsethUtilization, RagerUtilization, GaretzUtilization, GaretzGravityFactor, GaretzElevationFactor,
    GaretzHoppingRateFactor, WhirlpoolFactor, AdditionIbu,
    // Color
    MaltColorUnits, MoreySrm, DanielsSrm, MosherSrm,
    // Extract and efficiency
    ExtractMass, PotentialExtract, ExpectedExtract, Efficiency, AsIsYield,
    // Brew-day volumes
    KnockoutVolume, PostBoilVolume, PreBoilVolume, TotalWater, StrikeWater, SpargeWater, MashVolume,
    // Water
    SaltIons, Alkalinity, ResidualAlkalinity, SulfateChlorideRatio, CrystalMaltPh, AcidStrength, MashPh,
    // Yeast
    CellsNeeded, LiquidYeastViability, BraukaiserGrowth, WhiteGrowth,
    // TTB
    ExciseTax,
};
inline constexpr std::size_t kFormulaCount = static_cast<std::size_t>(Formula::ExciseTax) + 1;

struct FormulaInput {
    std::string_view symbol;   // LaTeX, as the formula writes it
    std::string_view meaning;  // "original gravity (SG)"
    double example = 0.0;      // a typical value, for trying the formula out
};

struct FormulaInfo {
    Formula formula;
    std::string_view key;     // saved with an adjustment: never change once shipped
    std::string_view group;   // "Gravity", "Alcohol"...
    std::string_view name;    // "Specific gravity to °Plato"
    std::string_view result;  // LaTeX for what it computes, shown left of the =
    std::string_view latex;   // the original formula: the right side of the =
    std::span<const FormulaInput> inputs;
    std::string_view note;    // where it comes from, and how Ninkasi uses it
    bool adjustable = true;
};

// Every formula, in enum order (grouped).
std::span<const FormulaInfo> formulas();
const FormulaInfo& info(Formula formula);
std::optional<Formula> formula_from_key(std::string_view key);

// Compiles `latex` as a version of `formula`, with its inputs. Throws
// FormulaError if it can't be read or computed, or the formula is reference only.
Expression compile_formula(Formula formula, std::string_view latex);

// --- Adjusted formulas -------------------------------------------------------------------
// The brewery's adjustments, used everywhere in place of the originals. The
// app installs them when it starts and whenever an admin changes one.

// nullopt goes back to the original.
void set_adjusted_formula(Formula formula, std::optional<Expression> adjusted);
void clear_adjusted_formulas();
// nullptr when the original is in use.
const Expression* adjusted_formula(Formula formula);

// The adjusted formula's value for `inputs` (in the formula's order) if the
// brewery has adjusted `formula`, and otherwise original().
template <typename Original>
double with_formula(Formula formula, std::initializer_list<double> inputs, Original original) {
    if (const Expression* adjusted = adjusted_formula(formula)) return adjusted->evaluate(inputs);
    return original();
}

} // namespace ninkasi::calc
