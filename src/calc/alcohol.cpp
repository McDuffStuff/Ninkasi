#include "calc/alcohol.h"

#include "calc/common.h"
#include "calc/gravity.h"

namespace ninkasi::calc {

namespace {

// Ethanol at 20 °C (0.78924 kg/L) relative to water at 20 °C.
constexpr double kEthanolSpecificGravity = 0.78924 / kWaterDensityKgPerL;

} // namespace

double abv(double og, double fg, AbvFormula formula) {
    switch (formula) {
    case AbvFormula::Simple:
        return (og - fg) * 131.25 / 100.0;
    case AbvFormula::Alternate:
        return (76.08 * (og - fg) / (1.775 - og)) * (fg / 0.794) / 100.0;
    case AbvFormula::Balling:
        return abw_to_abv(abw(og, fg), fg);
    }
    return kInvalid;
}

double real_extract_plato(double og, double fg) {
    return 0.1808 * sg_to_plato(og) + 0.8192 * sg_to_plato(fg);
}

double abw(double og, double fg) {
    const double original_extract = sg_to_plato(og);
    const double real_extract = real_extract_plato(og, fg);
    return (original_extract - real_extract) / (2.0665 - 0.010665 * original_extract) / 100.0;
}

double abw_to_abv(double alcohol_by_weight, double fg) {
    return alcohol_by_weight * fg / kEthanolSpecificGravity;
}

double apparent_attenuation(double og, double fg) {
    const double original_extract = sg_to_plato(og);
    if (!(original_extract > 0.0)) return kInvalid;
    return (original_extract - sg_to_plato(fg)) / original_extract;
}

double real_attenuation(double og, double fg) {
    const double original_extract = sg_to_plato(og);
    if (!(original_extract > 0.0)) return kInvalid;
    return (original_extract - real_extract_plato(og, fg)) / original_extract;
}

double predict_fg(double og, double attenuation) {
    return plato_to_sg(sg_to_plato(og) * (1.0 - attenuation));
}

} // namespace ninkasi::calc
