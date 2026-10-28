#include "calc/bitterness.h"

#include "calc/common.h"
#include "calc/formulas.h"

#include <algorithm>
#include <numeric>

namespace ninkasi::calc {

// --- Building blocks --------------------------------------------------------

double tinseth_utilization(double boil_gravity_sg, double minutes) {
    return with_formula(Formula::TinsethUtilization, {boil_gravity_sg, minutes}, [&] {
        const double bigness_factor = 1.65 * std::pow(0.000125, boil_gravity_sg - 1.0);
        const double boil_time_factor = (1.0 - std::exp(-0.04 * minutes)) / 4.15;
        return bigness_factor * boil_time_factor;
    });
}

double rager_utilization(double boil_gravity_sg, double minutes) {
    return with_formula(Formula::RagerUtilization, {boil_gravity_sg, minutes}, [&] {
        const double utilization = (18.11 + 13.86 * std::tanh((minutes - 31.32) / 18.27)) / 100.0;
        const double gravity_adjustment = boil_gravity_sg > 1.050 ? (boil_gravity_sg - 1.050) / 0.2 : 0.0;
        return utilization / (1.0 + gravity_adjustment);
    });
}

double garetz_table_utilization(double minutes) {
    // Utilization from this many minutes onward.
    struct Step {
        double from_min;
        double utilization;
    };
    static constexpr Step kTable[] = {
        {11, 0.02}, {16, 0.05}, {21, 0.08}, {26, 0.11}, {31, 0.14}, {36, 0.16},
        {41, 0.18}, {46, 0.19}, {51, 0.20}, {61, 0.21}, {71, 0.22}, {81, 0.23},
    };
    double utilization = 0.0;
    for (const Step& step : kTable) {
        if (minutes >= step.from_min) utilization = step.utilization;
    }
    return utilization;
}

double whirlpool_temperature_factor(double temperature_c) {
    const double kelvin = temperature_c + 273.15;
    if (!(kelvin > 0.0)) return kInvalid;
    return with_formula(Formula::WhirlpoolFactor, {temperature_c},
                        [&] { return std::min(1.0, 2.39e11 * std::exp(-9773.0 / kelvin)); });
}

// --- IBU --------------------------------------------------------------------

std::vector<double> addition_ibus(std::span<const HopAddition> additions, const BoilConditions& boil,
                                  const IbuSettings& settings) {
    std::vector<double> ibus(additions.size(), kInvalid);
    if (!(boil.volume_l > 0.0) || !(boil.boil_time_min >= 0.0)) return ibus;

    // Garetz's correction factors that don't depend on the hops themselves.
    const double gravity_factor = with_formula(Formula::GaretzGravityFactor, {boil.boil_gravity_sg},
                                               [&] { return (boil.boil_gravity_sg - 1.050) / 0.2 + 1.0; });
    const double elevation_ft = settings.elevation_m / 0.3048;
    const double temperature_factor = with_formula(Formula::GaretzElevationFactor, {elevation_ft},
                                                   [&] { return elevation_ft / 550.0 * 0.02 + 1.0; });

    for (std::size_t i = 0; i < additions.size(); ++i) {
        const HopAddition& hop = additions[i];
        if (!(hop.mass_kg >= 0.0) || !(hop.alpha_acid >= 0.0) || !(hop.time_min >= 0.0)) continue;

        // How long the hops isomerize, and how effectively compared with a
        // boil addition of that length.
        double minutes = hop.time_min;
        double use_factor = 1.0;
        switch (hop.use) {
        case HopUse::Boil:
            break;
        case HopUse::FirstWort:
            minutes = boil.boil_time_min;
            use_factor = settings.first_wort_factor;
            break;
        case HopUse::Mash:
            minutes = boil.boil_time_min;
            use_factor = settings.mash_hop_factor;
            break;
        case HopUse::Whirlpool:
            use_factor = whirlpool_temperature_factor(hop.temperature_c);
            break;
        case HopUse::DryHop:
            use_factor = 0.0;
            break;
        }

        double utilization = 0.0;
        switch (settings.formula) {
        case IbuFormula::Tinseth:
            utilization = tinseth_utilization(boil.boil_gravity_sg, minutes);
            break;
        case IbuFormula::Rager:
            utilization = rager_utilization(boil.boil_gravity_sg, minutes);
            break;
        case IbuFormula::Garetz: {
            // The hopping-rate factor is applied below, once the total is known.
            const double table = garetz_table_utilization(minutes);
            utilization = with_formula(Formula::GaretzUtilization, {table, gravity_factor, temperature_factor},
                                       [&] { return table / (gravity_factor * temperature_factor); });
            break;
        }
        }

        const double form_factor = settings.form_factor[static_cast<std::size_t>(hop.form)];
        ibus[i] = with_formula(Formula::AdditionIbu,
                               {utilization, use_factor, form_factor, settings.utilization_scale, hop.alpha_acid,
                                hop.mass_kg, boil.volume_l},
                               [&] {
                                   const double alpha_acid_mg_per_l =
                                       hop.alpha_acid * hop.mass_kg * 1.0e6 / boil.volume_l;
                                   return utilization * use_factor * form_factor * settings.utilization_scale *
                                          alpha_acid_mg_per_l;
                               });
    }

    if (settings.formula == IbuFormula::Garetz) {
        // Garetz's hopping-rate factor is HF = 1 + c·IBU with c = CF / 260,
        // where CF = final volume / boil volume and IBU is the total being
        // computed. The total therefore satisfies IBU = A / (1 + c·IBU), with
        // A the total before HF; solving that quadratic gives it directly.
        const double concentration = boil.boil_volume_l > 0.0 ? boil.volume_l / boil.boil_volume_l : 1.0;
        const double c = concentration / 260.0;
        const double unadjusted_total = std::accumulate(ibus.begin(), ibus.end(), 0.0);
        double hopping_rate_factor = 0.0;
        if (const Expression* adjusted = adjusted_formula(Formula::GaretzHoppingRateFactor)) {
            // An adjusted factor is solved by repeating IBU = A / HF(IBU) until it settles.
            double total = unadjusted_total;
            for (int step = 0; step < 200; ++step) {
                const double next = unadjusted_total / adjusted->evaluate({concentration, total});
                if (!is_valid(next) || std::abs(next - total) < 1e-9) {
                    total = next;
                    break;
                }
                total = next;
            }
            hopping_rate_factor = adjusted->evaluate({concentration, total});
        } else {
            const double total = (std::sqrt(1.0 + 4.0 * c * unadjusted_total) - 1.0) / (2.0 * c);
            hopping_rate_factor = 1.0 + c * total;
        }
        for (double& ibu : ibus) ibu /= hopping_rate_factor;
    }
    return ibus;
}

double total_ibu(std::span<const HopAddition> additions, const BoilConditions& boil, const IbuSettings& settings) {
    const std::vector<double> ibus = addition_ibus(additions, boil, settings);
    return std::accumulate(ibus.begin(), ibus.end(), 0.0);
}

} // namespace ninkasi::calc
