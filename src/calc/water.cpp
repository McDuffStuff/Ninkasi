#include "calc/water.h"

#include "calc/common.h"

#include <algorithm>
#include <array>
#include <iterator>

namespace ninkasi::calc {

namespace {

// Atomic and ionic masses, g/mol.
constexpr double kCalcium = 40.078;
constexpr double kMagnesium = 24.305;
constexpr double kSodium = 22.98977;
constexpr double kChlorine = 35.453;
constexpr double kHydrogen = 1.008;
constexpr double kCarbon = 12.011;
constexpr double kOxygen = 15.999;
constexpr double kPhosphorus = 30.974;
constexpr double kSulfate = 32.065 + 4 * kOxygen;
constexpr double kBicarbonate = kHydrogen + kCarbon + 3 * kOxygen;
constexpr double kWater = 2 * kHydrogen + kOxygen;
constexpr double kCalciumCarbonate = kCalcium + kCarbon + 3 * kOxygen;

// Equivalent weights (g per equivalent) for alkalinity and hardness.
constexpr double kCalciumCarbonateEq = kCalciumCarbonate / 2.0;
constexpr double kCalciumEq = kCalcium / 2.0;
constexpr double kMagnesiumEq = kMagnesium / 2.0;

// Moles of each ion released per mole of salt.
struct SaltRow {
    SaltInfo info;
    double molar_mass;
    double calcium, magnesium, sodium, chloride, sulfate, bicarbonate;
};

constexpr SaltRow kSalts[] = {
    {{Salt::Gypsum, "gypsum", "Gypsum", "CaSO4·2H2O"},
     kCalcium + kSulfate + 2 * kWater, 1, 0, 0, 0, 1, 0},
    {{Salt::CalciumChloride, "calcium_chloride", "Calcium chloride", "CaCl2·2H2O"},
     kCalcium + 2 * kChlorine + 2 * kWater, 1, 0, 0, 2, 0, 0},
    {{Salt::EpsomSalt, "epsom_salt", "Epsom salt", "MgSO4·7H2O"},
     kMagnesium + kSulfate + 7 * kWater, 0, 1, 0, 0, 1, 0},
    {{Salt::MagnesiumChloride, "magnesium_chloride", "Magnesium chloride", "MgCl2·6H2O"},
     kMagnesium + 2 * kChlorine + 6 * kWater, 0, 1, 0, 2, 0, 0},
    {{Salt::TableSalt, "table_salt", "Table salt", "NaCl"},
     kSodium + kChlorine, 0, 0, 1, 1, 0, 0},
    {{Salt::BakingSoda, "baking_soda", "Baking soda", "NaHCO3"},
     kSodium + kBicarbonate, 0, 0, 1, 0, 0, 1},
    // Dissolved with CO2: CaCO3 + CO2 + H2O -> Ca2+ + 2 HCO3-.
    {{Salt::Chalk, "chalk", "Chalk", "CaCO3"},
     kCalciumCarbonate, 1, 0, 0, 0, 0, 2},
};

constexpr auto kSaltInfos = [] {
    std::array<SaltInfo, std::size(kSalts)> infos{};
    for (std::size_t i = 0; i < infos.size(); ++i) infos[i] = kSalts[i].info;
    return infos;
}();

struct AcidRow {
    AcidInfo info;
    double mass_fraction;
    double density_g_per_ml;
    double molar_mass;
    std::array<double, 3> pka;
    int protons;
};

constexpr double kLacticAcid = 3 * kCarbon + 6 * kHydrogen + 3 * kOxygen;
constexpr double kPhosphoricAcid = 3 * kHydrogen + kPhosphorus + 4 * kOxygen;

constexpr AcidRow kAcids[] = {
    {{Acid::Lactic88, "lactic_88", "Lactic acid 88 %"}, 0.88, 1.209, kLacticAcid, {3.86, 0, 0}, 1},
    {{Acid::Phosphoric10, "phosphoric_10", "Phosphoric acid 10 %"}, 0.10, 1.053, kPhosphoricAcid, {2.15, 7.20, 12.35}, 3},
    {{Acid::Phosphoric85, "phosphoric_85", "Phosphoric acid 85 %"}, 0.85, 1.685, kPhosphoricAcid, {2.15, 7.20, 12.35}, 3},
};

constexpr auto kAcidInfos = [] {
    std::array<AcidInfo, std::size(kAcids)> infos{};
    for (std::size_t i = 0; i < infos.size(); ++i) infos[i] = kAcids[i].info;
    return infos;
}();

constexpr bool tables_match_enum_order() {
    for (std::size_t i = 0; i < std::size(kSalts); ++i) {
        if (static_cast<std::size_t>(kSalts[i].info.salt) != i) return false;
    }
    for (std::size_t i = 0; i < std::size(kAcids); ++i) {
        if (static_cast<std::size_t>(kAcids[i].info.acid) != i) return false;
    }
    return true;
}
static_assert(tables_match_enum_order(), "salt and acid tables must follow enum order");

const SaltRow& row(Salt salt) { return kSalts[static_cast<std::size_t>(salt)]; }
const AcidRow& row(Acid acid) { return kAcids[static_cast<std::size_t>(acid)]; }

// Average number of protons an acid has given up at `ph`.
double protons_released(const AcidRow& acid, double ph) {
    const double h = std::pow(10.0, -ph);
    double species = 1.0;   // the fully protonated form, relative
    double weighted = 0.0;
    double total = 1.0;
    for (int j = 0; j < acid.protons; ++j) {
        species *= std::pow(10.0, -acid.pka[static_cast<std::size_t>(j)]) / h;
        weighted += (j + 1) * species;
        total += species;
    }
    return weighted / total;
}

double lactic_acid_meq(double lactic_acid_g, double ph) {
    return lactic_acid_g / kLacticAcid * 1000.0 * protons_released(row(Acid::Lactic88), ph);
}

WaterProfile invalid_profile() {
    return {kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid};
}

} // namespace

// --- Salts ------------------------------------------------------------------

std::span<const SaltInfo> salts() { return kSaltInfos; }
const SaltInfo& info(Salt salt) { return row(salt).info; }

std::optional<Salt> salt_from_key(std::string_view key) {
    for (const SaltInfo& salt : kSaltInfos) {
        if (salt.key == key) return salt.salt;
    }
    return std::nullopt;
}

WaterProfile add_salts(const WaterProfile& water, std::span<const SaltAddition> additions, double volume_l) {
    if (!(volume_l > 0.0)) return invalid_profile();
    WaterProfile result = water;
    for (const SaltAddition& addition : additions) {
        if (!(addition.mass_kg >= 0.0)) return invalid_profile();
        const SaltRow& salt = row(addition.salt);
        const double mmol_per_l = addition.mass_kg * 1.0e6 / salt.molar_mass / volume_l;
        result.calcium += mmol_per_l * salt.calcium * kCalcium;
        result.magnesium += mmol_per_l * salt.magnesium * kMagnesium;
        result.sodium += mmol_per_l * salt.sodium * kSodium;
        result.chloride += mmol_per_l * salt.chloride * kChlorine;
        result.sulfate += mmol_per_l * salt.sulfate * kSulfate;
        result.bicarbonate += mmol_per_l * salt.bicarbonate * kBicarbonate;
    }
    return result;
}

WaterProfile blend(const WaterProfile& a, double a_volume_l, const WaterProfile& b, double b_volume_l) {
    const double total = a_volume_l + b_volume_l;
    if (!(a_volume_l >= 0.0) || !(b_volume_l >= 0.0) || !(total > 0.0)) return invalid_profile();
    auto mix = [&](double x, double y) { return (x * a_volume_l + y * b_volume_l) / total; };
    return {mix(a.calcium, b.calcium),   mix(a.magnesium, b.magnesium), mix(a.sodium, b.sodium),
            mix(a.chloride, b.chloride), mix(a.sulfate, b.sulfate),     mix(a.bicarbonate, b.bicarbonate)};
}

double alkalinity(const WaterProfile& water) {
    return water.bicarbonate / kBicarbonate * kCalciumCarbonateEq;
}

double residual_alkalinity(const WaterProfile& water) {
    const double meq_per_l = water.bicarbonate / kBicarbonate - water.calcium / kCalciumEq / 3.5 -
                             water.magnesium / kMagnesiumEq / 7.0;
    return meq_per_l * kCalciumCarbonateEq;
}

double sulfate_chloride_ratio(const WaterProfile& water) {
    if (!(water.chloride > 0.0)) return kInvalid;
    return water.sulfate / water.chloride;
}

// --- Acids ------------------------------------------------------------------

std::span<const AcidInfo> acids() { return kAcidInfos; }
const AcidInfo& info(Acid acid) { return row(acid).info; }

std::optional<Acid> acid_from_key(std::string_view key) {
    for (const AcidInfo& acid : kAcidInfos) {
        if (acid.key == key) return acid.acid;
    }
    return std::nullopt;
}

double acid_meq(Acid acid, double volume_l, double ph) {
    const AcidRow& a = row(acid);
    const double moles = volume_l * 1000.0 * a.density_g_per_ml * a.mass_fraction / a.molar_mass;
    return moles * 1000.0 * protons_released(a, ph);
}

double acid_volume_l(Acid acid, double meq, double ph) {
    return meq / acid_meq(acid, 1.0, ph);
}

// --- Mash pH ----------------------------------------------------------------

double default_di_ph(MaltCategory category, double color_lovibond) {
    switch (category) {
    case MaltCategory::Base:
    case MaltCategory::Acidulated:  // its lactic acid is counted separately
        return 5.7;
    case MaltCategory::Crystal:
        return std::clamp(5.38 - 0.0144 * (color_lovibond - 10.0), 4.5, 5.6);
    case MaltCategory::Roasted:
        return 4.7;
    }
    return 5.7;
}

namespace {

// The parts of the charge balance that don't depend on the mash pH.
struct MashBalance {
    double buffering = 0.0;      // Σ buffering × mass, mEq/pH
    double weighted_ph = 0.0;    // Σ buffering × mass × DI pH
    double water_base_meq = 0.0; // residual alkalinity of the mash water
    double lactic_acid_g = 0.0;  // from acidulated malt
};

std::optional<MashBalance> mash_balance(std::span<const MashGrain> grains, const WaterProfile& water, double water_l) {
    if (!(water_l >= 0.0)) return std::nullopt;
    MashBalance balance;
    for (const MashGrain& grain : grains) {
        if (!(grain.mass_kg >= 0.0)) return std::nullopt;
        const double buffering = grain.buffering.value_or(kDefaultMaltBuffering) * grain.mass_kg;
        balance.buffering += buffering;
        balance.weighted_ph += buffering * grain.di_ph.value_or(default_di_ph(grain.category, grain.color_lovibond));
        if (grain.category == MaltCategory::Acidulated) {
            balance.lactic_acid_g += grain.mass_kg * 1000.0 * kAcidMaltLacticFraction;
        }
    }
    if (!(balance.buffering > 0.0)) return std::nullopt;
    balance.water_base_meq = residual_alkalinity(water) / kCalciumCarbonateEq * water_l;
    return balance;
}

double total_acid_meq(const MashBalance& balance, std::span<const AcidAddition> acids, double ph) {
    double meq = lactic_acid_meq(balance.lactic_acid_g, ph);
    for (const AcidAddition& acid : acids) meq += acid_meq(acid.acid, acid.volume_l, ph);
    return meq;
}

} // namespace

double estimate_mash_ph(std::span<const MashGrain> grains, const WaterProfile& water, double water_l,
                        std::span<const AcidAddition> acids) {
    const std::optional<MashBalance> balance = mash_balance(grains, water, water_l);
    if (!balance) return kInvalid;

    // Balance at pH p:  Σ BC·m·(p − pH_DI) = water base − acid(p).
    // Acids give up slightly more protons at higher pH, so iterate.
    double ph = 5.4;
    for (int i = 0; i < 50; ++i) {
        const double next =
            (balance->weighted_ph + balance->water_base_meq - total_acid_meq(*balance, acids, ph)) / balance->buffering;
        if (std::abs(next - ph) < 1e-10) return next;
        ph = next;
    }
    return ph;
}

double acid_needed_meq(std::span<const MashGrain> grains, const WaterProfile& water, double water_l,
                       std::span<const AcidAddition> acids, double target_ph) {
    const std::optional<MashBalance> balance = mash_balance(grains, water, water_l);
    if (!balance) return kInvalid;
    return balance->weighted_ph + balance->water_base_meq - total_acid_meq(*balance, acids, target_ph) -
           balance->buffering * target_ph;
}

} // namespace ninkasi::calc
