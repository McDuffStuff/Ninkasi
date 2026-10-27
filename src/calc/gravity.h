#pragma once
// Gravity scales and gravity measurements.
//
// Specific gravity (SG, 20 °C/20 °C) is canonical. Degrees Plato (% extract
// by weight) and degrees Brix (% sucrose by weight) are treated as the same
// scale: over the brewing range they differ by less than 0.02°.

namespace ninkasi::calc {

// --- Scales -----------------------------------------------------------------

// SG → °Plato with the cubic fit to the ASBC/sucrose tables that brewing
// software widely uses (within 0.01° of the tables from 1.000 to 1.100):
//   °P = −616.868 + 1111.14·SG − 630.272·SG² + 135.997·SG³
double sg_to_plato(double sg);

// °Plato → SG: the exact inverse of sg_to_plato (solved numerically), so
// values round-trip without drifting.
double plato_to_sg(double plato);

inline double sg_to_brix(double sg) { return sg_to_plato(sg); }
inline double brix_to_sg(double brix) { return plato_to_sg(brix); }

// "Gravity points": 1.050 → 50.
inline double gravity_points(double sg) { return (sg - 1.0) * 1000.0; }

// --- Hydrometers ------------------------------------------------------------

// Corrects a hydrometer reading taken at `sample_temp_c` for an instrument
// calibrated at `calibration_temp_c` (marked on the hydrometer: usually
// 15.56 °C / 60 °F or 20 °C / 68 °F). Warm samples read low, cold ones high.
// Uses the widely published polynomial for water density versus temperature.
double hydrometer_corrected_sg(double reading_sg, double sample_temp_c, double calibration_temp_c);

// --- Refractometers ---------------------------------------------------------
//
// Refractometers are scaled for sucrose; wort reads high by the wort
// correction factor (WCF), typically 1.02–1.06. Calibrate yours with
// wort_correction_factor() on a sample of unfermented wort.

inline constexpr double kDefaultWortCorrectionFactor = 1.04;

// Gravity of unfermented wort from a refractometer reading in °Brix.
double refractometer_wort_sg(double brix_reading, double wcf);

// Gravity of fermenting or finished beer. Alcohol distorts the reading, so the
// original (pre-fermentation) reading is needed too. Sean Terrill's cubic:
//   SG = 1 − 0.0044993·OB + 0.011774·FB + 0.00027581·OB² − 0.0012717·FB²
//          − 0.0000072800·OB³ + 0.000063293·FB³
// where OB and FB are the original and current readings divided by the WCF.
double refractometer_fermenting_sg(double original_brix_reading, double current_brix_reading,
                                   double wcf);

// The WCF of a refractometer: its reading divided by the °Plato of a
// hydrometer reading of the same unfermented wort.
double wort_correction_factor(double brix_reading, double hydrometer_sg);

} // namespace ninkasi::calc
