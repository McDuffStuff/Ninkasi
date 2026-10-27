#pragma once
// Bitterness: estimating IBUs from hop additions.
//
// IBU = utilization × mg/L of alpha acids added. Three published models give
// the utilization; Tinseth is the default:
//   Tinseth                1.65 · 0.000125^(G − 1) × (1 − e^(−0.04·t)) / 4.15
//   Rager (Zymurgy, 1990)  18.11 % + 13.86 % · tanh((t − 31.32) / 18.27),
//                          divided by 1 + (G − 1.050) / 0.2 when G > 1.050
//   Garetz (Using Hops)    tabulated by boil time, divided by gravity,
//                          hopping-rate and elevation factors
// where G is the boil gravity and t the boil time in minutes.
//
// Whirlpool / hop-stand additions use the model's utilization for the steep
// time, scaled by Hosom's (2015) temperature factor 2.39e11 · e^(−9773 / T[K]).

#include <array>
#include <cstddef>
#include <span>
#include <vector>

namespace ninkasi::calc {

enum class IbuFormula { Tinseth, Rager, Garetz };

enum class HopUse {
    Boil,       // time = minutes before flameout
    FirstWort,  // in the kettle while the wort is collected; boils for the whole boil
    Whirlpool,  // added at or after flameout and steeped hot; time = steep minutes
    Mash,       // added to the mash
    DryHop,     // added to the fermenter; adds no IBU
};

enum class HopForm { Pellet, Leaf, Plug, Powder, Extract };
inline constexpr std::size_t kHopFormCount = 5;

struct HopAddition {
    double mass_kg = 0.0;
    double alpha_acid = 0.0;        // fraction: 0.055 = 5.5 %
    HopUse use = HopUse::Boil;
    HopForm form = HopForm::Pellet;
    double time_min = 0.0;          // Boil: before flameout. Whirlpool: steep time.
    double temperature_c = 100.0;   // Whirlpool only: steep temperature
};

struct BoilConditions {
    double boil_gravity_sg = 1.050; // wort gravity during the boil
    double volume_l = 0.0;          // volume the IBUs are for, normally post-boil
    double boil_volume_l = 0.0;     // Garetz only: volume at the start of the boil (0 = volume_l)
    double boil_time_min = 60.0;    // used for first-wort and mash hops
};

// Brewer-adjustable knobs. The defaults leave the published formulas
// unchanged; mash hops have no published model, so 0.2 is a rule of thumb.
struct IbuSettings {
    IbuFormula formula = IbuFormula::Tinseth;
    // Calibrates the model to a brewhouse, e.g. from lab-measured IBUs.
    double utilization_scale = 1.0;
    // Relative utilization of each HopForm, indexed by HopForm. Some brewers
    // give pellets ~1.1.
    std::array<double, kHopFormCount> form_factor = {1.0, 1.0, 1.0, 1.0, 1.0};
    // First-wort hops relative to a full-length boil addition (often 1.0–1.1).
    double first_wort_factor = 1.0;
    // Mash hops relative to a full-length boil addition.
    double mash_hop_factor = 0.2;
    // Garetz only: brewery elevation above sea level, in meters.
    double elevation_m = 0.0;

    bool operator==(const IbuSettings&) const = default;
};

// The IBUs from each addition, in the same order. They are computed together
// because Garetz's hopping-rate factor depends on the total.
std::vector<double> addition_ibus(std::span<const HopAddition> additions, const BoilConditions& boil,
                                  const IbuSettings& settings = {});

double total_ibu(std::span<const HopAddition> additions, const BoilConditions& boil,
                 const IbuSettings& settings = {});

// --- Building blocks (also used to explain the numbers in the UI) ----------

// Fraction of alpha acids that end up as iso-alpha acids in the beer.
double tinseth_utilization(double boil_gravity_sg, double minutes);
double rager_utilization(double boil_gravity_sg, double minutes);
// Garetz's table before his correction factors.
double garetz_table_utilization(double minutes);

// Isomerization at `temperature_c` relative to a rolling boil (capped at 1).
double whirlpool_temperature_factor(double temperature_c);

} // namespace ninkasi::calc
