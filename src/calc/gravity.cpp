#include "calc/gravity.h"

#include "calc/common.h"
#include "calc/formulas.h"

namespace ninkasi::calc {

// --- Scales -----------------------------------------------------------------

double sg_to_plato(double sg) {
    return with_formula(Formula::SgToPlato, {sg},
                        [&] { return -616.868 + sg * (1111.14 + sg * (-630.272 + sg * 135.997)); });
}

namespace {

double sg_to_plato_slope(double sg) {
    return 1111.14 + sg * (-2.0 * 630.272 + sg * 3.0 * 135.997);
}

} // namespace

double plato_to_sg(double plato) {
    if (!is_valid(plato)) return kInvalid;
    // Newton's method on the cubic. Its slope is positive for every gravity
    // (minimum ≈ 137 °P per SG unit), so this converges in a few steps.
    // An adjusted formula's slope is found numerically.
    const bool adjusted = adjusted_formula(Formula::SgToPlato) != nullptr;
    double sg = 1.0 + plato / 250.0;  // rule of thumb: 1 °P ≈ 4 gravity points
    for (int i = 0; i < 50; ++i) {
        const double slope = adjusted ? (sg_to_plato(sg + 1e-6) - sg_to_plato(sg - 1e-6)) / 2e-6 : sg_to_plato_slope(sg);
        const double step = (sg_to_plato(sg) - plato) / slope;
        if (!is_valid(step)) return kInvalid;
        sg -= step;
        if (std::abs(step) < 1e-13) break;
    }
    return sg;
}

// --- Hydrometers ------------------------------------------------------------

namespace {

// Fit to water's expansion with temperature (°F); the ratio of two values is
// the correction between those temperatures.
double water_expansion(double temp_f) {
    return with_formula(Formula::WaterDensity, {temp_f}, [&] {
        return 1.00130346 - 0.000134722124 * temp_f + 0.00000204052596 * temp_f * temp_f -
               0.00000000232820948 * temp_f * temp_f * temp_f;
    });
}

double celsius_to_fahrenheit(double temp_c) {
    return temp_c * 9.0 / 5.0 + 32.0;
}

} // namespace

double hydrometer_corrected_sg(double reading_sg, double sample_temp_c, double calibration_temp_c) {
    const double sample = water_expansion(celsius_to_fahrenheit(sample_temp_c));
    const double calibration = water_expansion(celsius_to_fahrenheit(calibration_temp_c));
    return with_formula(Formula::HydrometerCorrection, {reading_sg, sample, calibration},
                        [&] { return reading_sg * sample / calibration; });
}

// --- Refractometers ---------------------------------------------------------

double refractometer_wort_sg(double brix_reading, double wcf) {
    if (!(wcf > 0.0)) return kInvalid;
    return plato_to_sg(with_formula(Formula::RefractometerWort, {brix_reading, wcf}, [&] { return brix_reading / wcf; }));
}

double refractometer_fermenting_sg(double original_brix_reading, double current_brix_reading,
                                   double wcf) {
    if (!(wcf > 0.0)) return kInvalid;
    const double ob = original_brix_reading / wcf;
    const double fb = current_brix_reading / wcf;
    return with_formula(Formula::RefractometerFermenting, {ob, fb}, [&] {
        return 1.0 - 0.0044993 * ob + 0.011774 * fb + 0.00027581 * ob * ob - 0.0012717 * fb * fb -
               0.0000072800 * ob * ob * ob + 0.000063293 * fb * fb * fb;
    });
}

double wort_correction_factor(double brix_reading, double hydrometer_sg) {
    const double plato = sg_to_plato(hydrometer_sg);
    if (!(plato > 0.0)) return kInvalid;
    return with_formula(Formula::WortCorrectionFactor, {brix_reading, plato}, [&] { return brix_reading / plato; });
}

} // namespace ninkasi::calc
