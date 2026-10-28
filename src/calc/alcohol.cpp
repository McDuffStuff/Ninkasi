#include "calc/alcohol.h"

#include "calc/common.h"
#include "calc/formulas.h"
#include "calc/gravity.h"

namespace ninkasi::calc {

namespace {

// Ethanol at 20 °C (0.78924 kg/L) relative to water at 20 °C.
constexpr double kEthanolSpecificGravity = 0.78924 / kWaterDensityKgPerL;

} // namespace

double abv(double og, double fg, AbvFormula formula) {
    switch (formula) {
    case AbvFormula::Simple:
        return with_formula(Formula::AbvSimple, {og, fg}, [&] { return (og - fg) * 131.25; }) / 100.0;
    case AbvFormula::Alternate:
        return with_formula(Formula::AbvAlternate, {og, fg},
                            [&] { return (76.08 * (og - fg) / (1.775 - og)) * (fg / 0.794); }) /
               100.0;
    case AbvFormula::Balling:
        return abw_to_abv(abw(og, fg), fg);
    }
    return kInvalid;
}

double real_extract_plato(double og, double fg) {
    const double original_extract = sg_to_plato(og);
    const double apparent_extract = sg_to_plato(fg);
    return with_formula(Formula::RealExtract, {original_extract, apparent_extract},
                        [&] { return 0.1808 * original_extract + 0.8192 * apparent_extract; });
}

double abw(double og, double fg) {
    const double original_extract = sg_to_plato(og);
    const double real_extract = real_extract_plato(og, fg);
    return with_formula(Formula::AlcoholByWeight, {original_extract, real_extract},
                        [&] { return (original_extract - real_extract) / (2.0665 - 0.010665 * original_extract); }) /
           100.0;
}

double abw_to_abv(double alcohol_by_weight, double fg) {
    // The formula is in percent.
    const double percent = alcohol_by_weight * 100.0;
    return with_formula(Formula::AbwToAbv, {percent, fg}, [&] { return percent * fg / kEthanolSpecificGravity; }) /
           100.0;
}

double apparent_attenuation(double og, double fg) {
    const double original_extract = sg_to_plato(og);
    if (!(original_extract > 0.0)) return kInvalid;
    const double apparent_extract = sg_to_plato(fg);
    return with_formula(Formula::ApparentAttenuation, {original_extract, apparent_extract},
                        [&] { return (original_extract - apparent_extract) / original_extract; });
}

double real_attenuation(double og, double fg) {
    const double original_extract = sg_to_plato(og);
    if (!(original_extract > 0.0)) return kInvalid;
    const double real_extract = real_extract_plato(og, fg);
    return with_formula(Formula::RealAttenuation, {original_extract, real_extract},
                        [&] { return (original_extract - real_extract) / original_extract; });
}

double predict_fg(double og, double attenuation) {
    const double original_extract = sg_to_plato(og);
    return plato_to_sg(with_formula(Formula::PredictedFinalExtract, {original_extract, attenuation},
                                    [&] { return original_extract * (1.0 - attenuation); }));
}

} // namespace ninkasi::calc
