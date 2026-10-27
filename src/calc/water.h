#pragma once
// Brewing water chemistry: ions from salts, acids, residual alkalinity, and
// an estimate of mash pH.

#include <optional>
#include <span>
#include <string_view>

namespace ninkasi::calc {

// Ion concentrations in mg/L (ppm).
struct WaterProfile {
    double calcium = 0.0;
    double magnesium = 0.0;
    double sodium = 0.0;
    double chloride = 0.0;
    double sulfate = 0.0;
    double bicarbonate = 0.0;

    bool operator==(const WaterProfile&) const = default;
};

// --- Salts ------------------------------------------------------------------

enum class Salt {
    Gypsum,             // CaSO4·2H2O
    CalciumChloride,    // CaCl2·2H2O
    EpsomSalt,          // MgSO4·7H2O
    MagnesiumChloride,  // MgCl2·6H2O
    TableSalt,          // NaCl
    BakingSoda,         // NaHCO3
    Chalk,              // CaCO3: assumes it fully dissolves, which takes acid or CO2
};

struct SaltInfo {
    Salt salt;
    std::string_view key;  // saved with recipes: never change once shipped
    std::string_view name;
    std::string_view formula;
};

std::span<const SaltInfo> salts();
const SaltInfo& info(Salt salt);
std::optional<Salt> salt_from_key(std::string_view key);

struct SaltAddition {
    Salt salt = Salt::Gypsum;
    double mass_kg = 0.0;

    bool operator==(const SaltAddition&) const = default;
};

// `water` after dissolving `additions` into `volume_l` of it. Ion amounts
// come from each salt's molar mass (e.g. 1 g of gypsum per US gallon adds
// 61.5 ppm calcium and 147.4 ppm sulfate).
WaterProfile add_salts(const WaterProfile& water, std::span<const SaltAddition> additions, double volume_l);

// Mixture of two waters, e.g. tap water cut with RO water.
WaterProfile blend(const WaterProfile& a, double a_volume_l, const WaterProfile& b, double b_volume_l);

// Alkalinity as ppm CaCO3, from bicarbonate.
double alkalinity(const WaterProfile& water);

// Kolbach's residual alkalinity, ppm as CaCO3: the alkalinity left after
// calcium and magnesium react with malt phosphates
// (RA = alkalinity − Ca/1.4 − Mg/1.7, with the ions in ppm).
double residual_alkalinity(const WaterProfile& water);

double sulfate_chloride_ratio(const WaterProfile& water);

// --- Acids ------------------------------------------------------------------

enum class Acid { Lactic88, Phosphoric10, Phosphoric85 };

struct AcidInfo {
    Acid acid;
    std::string_view key;  // saved with recipes: never change once shipped
    std::string_view name;
};

std::span<const AcidInfo> acids();
const AcidInfo& info(Acid acid);
std::optional<Acid> acid_from_key(std::string_view key);

struct AcidAddition {
    Acid acid = Acid::Lactic88;
    double volume_l = 0.0;

    bool operator==(const AcidAddition&) const = default;
};

// Milliequivalents of acid that `volume_l` of it delivers at `ph` (an acid
// gives up more protons the higher the pH), from its strength, density,
// molar mass and pKa values.
double acid_meq(Acid acid, double volume_l, double ph = 5.4);

// Volume of acid that delivers `meq` at `ph`.
double acid_volume_l(Acid acid, double meq, double ph = 5.4);

// --- Mash pH ----------------------------------------------------------------
//
// A charge balance (after Kai Troester's approach): each malt resists pH
// change with a buffering capacity around its distilled-water pH, and the
// water's residual alkalinity and any acids push against it. Treat the result
// as an estimate (±0.1–0.2): malts vary, so calibrate the per-malt values
// against your own pH readings.

enum class MaltCategory { Base, Crystal, Roasted, Acidulated };

inline constexpr double kDefaultMaltBuffering = 35.0;    // mEq/(kg·pH); malts measure roughly 30–50
inline constexpr double kAcidMaltLacticFraction = 0.03;  // acidulated malt is typically 2–3 % lactic acid

// Typical distilled-water mash pH: base malt 5.7, roasted 4.7, crystal
// falling with color from about 5.4 at 10 °L (rough fit, 4.5 minimum).
double default_di_ph(MaltCategory category, double color_lovibond);

struct MashGrain {
    double mass_kg = 0.0;
    MaltCategory category = MaltCategory::Base;
    double color_lovibond = 0.0;
    std::optional<double> di_ph;      // measured distilled-water pH, if known
    std::optional<double> buffering;  // mEq/(kg·pH), if known
};

// Estimated room-temperature mash pH for `grains` mashed in `water_l` of
// `water`, with `acids` added to the mash.
double estimate_mash_ph(std::span<const MashGrain> grains, const WaterProfile& water, double water_l,
                        std::span<const AcidAddition> acids = {});

// Extra acid (mEq; negative means base) needed to bring that mash to `target_ph`.
double acid_needed_meq(std::span<const MashGrain> grains, const WaterProfile& water, double water_l,
                       std::span<const AcidAddition> acids, double target_ph);

} // namespace ninkasi::calc
